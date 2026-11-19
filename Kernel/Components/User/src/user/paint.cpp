#include <kernel/gfx/gfx.hpp>
#include <kernel/gfx/fill.hpp>
#include <kernel/gfx/screen.hpp>
#include <kernel/user/user.hpp>

#include <kernel/gfx.hpp>
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KSurface.hpp>


namespace kernel::user {
namespace {
static_assert(sizeof(URect) == sizeof(KClipRect), "URect and KClipRect must share a layout");
uint32_t g_drawDepth{0};
uint32_t g_activePaints{0};

// The active target, refreshed only when the gfx topology generation moves.
// Callers hold the USER lock; the cached reference keeps the display alive
// for one USER operation. GFX ignores fills and presents to a display that
// is no longer active. This keeps the driver lock and the target refcount
// off every draw.
KObjectPtr<KDisplay> g_display;
uint32_t g_displayGeneration{0};
const KObjectPtr<KDisplay> kNoDisplay;

const KObjectPtr<KDisplay> &primary_display() noexcept {
  const uint32_t generation = gfx::topology_generation();
  if (generation != g_displayGeneration) {
    g_display = gfx::active_video_present_target();
    g_displayGeneration = generation;
  }
  if (!g_display || !g_display->surface || !g_display->isAttachedToDesktop)
    return kNoDisplay;
  return g_display;
}

URect bounds_of(const KDisplay &display) noexcept {
  return {.left = 0, .top = 0, .right = static_cast<int32_t>(display.surface->width), .bottom = static_cast<int32_t>(display.surface->height)};
}

uint64_t area(const URect &r) noexcept {
  return r.Empty() ? 0 : static_cast<uint64_t>(r.right - r.left) * static_cast<uint64_t>(r.bottom - r.top);
}

// Returns true when the pixels still have to be presented, i.e. they were
// written to the surface but not necessarily to the visible image.
// Inside a scene draw every fill stays in a packed shadow, so the frame
// appears atomically at the final present. A visible hardware fill would
// show caption bars before their deferred glyphs and bevels.
bool fill(const KObjectPtr<KDisplay> &display, const URect &r, uint32_t color) noexcept {
  return gfx::FillRectPaletteIndex(display, r.left, r.top, r.right - r.left, r.bottom - r.top, color,
                                   g_drawDepth ? gfx::FillMode::Deferred : gfx::FillMode::Auto);
}

bool fill_shadow(const KObjectPtr<KDisplay> &display, const URect &r,
                 uint32_t color) noexcept {
  return gfx::FillRectPaletteIndex(display, r.left, r.top, r.right - r.left, r.bottom - r.top, color,
                                   gfx::FillMode::ShadowOnly);
}

void present(const KObjectPtr<KDisplay> &display, kstd::span<const URect> rects) noexcept {
  gfx::PresentDisplay(display, rects);
}

// Copies `clip` into `storage`, the rectangles the DC reads, under the GFX
// scene lock. `target` non-null also selects the drawing surface.
kstd::error_code bind_dc_locked(KProcessObject *process, const KWindowObject &window, HDC dc, URegion &storage,
                                const URegion &clip, KSurface *target) noexcept {
  const URect origin = client_to_screen_locked(window, {});
  const auto bound = gfx::BindWindowDC(process, dc,
                                       {.target = target,
                                        .originX = origin.left,
                                        .originY = origin.top,
                                        .clipStorage = {reinterpret_cast<KClipRect *>(storage.rects),
                                                        URegion::kMaxRects},
                                        .clip = {reinterpret_cast<const KClipRect *>(clip.rects), clip.count},
                                        .hwnd = window.hwnd});
  if (bound)
    storage = clip;
  else
    storage.count = clip.count;
  return bound;
}

kstd::error_code bind_window_clip_locked(KWindowObject &window, KSurface *target) noexcept {
  URegion clip;
  visible_region_locked(window, clip);
  if (effectively_visible_locked(window))
    clip.Intersect(client_to_screen_locked(window, client_rect_locked(window)));
  else
    clip.Clear();
  return bind_dc_locked(window.process, window, window.windowDC, window.windowClip, clip, target);
}

// Damage produced while a scene draw is open. It is presented once when the
// outermost scene draw ends instead of once per frame line or glyph span.
// Rectangles may overlap; a rectangle that adds little area to an existing
// entry is merged into it, and overflow collapses the list to its bounds.
URegion g_frameDamage;

void add_frame_damage(const URect &r) noexcept {
  if (r.Empty())
    return;
  for (uint32_t i = 0; i < g_frameDamage.count; ++i) {
    auto &d = g_frameDamage.rects[i];
    const URect merged = d.Union(r);
    const uint64_t covered = area(d) + area(r);
    if (area(merged) <= covered + covered / 4) {
      d = merged;
      return;
    }
  }
  if (g_frameDamage.count < URegion::kMaxRects) {
    g_frameDamage.rects[g_frameDamage.count++] = r;
    return;
  }
  g_frameDamage.Set(g_frameDamage.Bounds().Union(r));
}

void present_region(const KObjectPtr<KDisplay> &display, const URegion &region) noexcept {
  if (region_merges_to_bounds(region)) {
    const URect bounds = region.Bounds();
    present(display, {&bounds, 1});
    return;
  }
  present(display, {region.rects, region.count});
}

// Presents now, or defers to the end of the open scene draw.
void damage_locked(const KObjectPtr<KDisplay> &display, const URect &r) noexcept {
  if (r.Empty())
    return;
  if (g_drawDepth) {
    add_frame_damage(r);
    return;
  }
  present(display, {&r, 1});
}

void flush_frame_damage_locked() noexcept {
  if (g_frameDamage.Empty())
    return;
  URegion pending = g_frameDamage;
  g_frameDamage.Clear();
  const auto &display = primary_display();
  if (!display)
    return;
  // A mode change inside the scene can shrink the screen under the damage.
  pending.Intersect(bounds_of(*display));
  if (!pending.Empty())
    present_region(display, pending);
}

}// namespace

void display_cache_reset_locked() noexcept {
  g_display = nullptr;
  g_displayGeneration = 0;
}

uint32_t active_paint_count_locked() noexcept { return g_activePaints; }

void begin_scene_draw_locked() noexcept {
  gfx::BeginScreenDraw();
  ++g_drawDepth;
}

void end_scene_draw_locked() noexcept {
  if (g_drawDepth)
    --g_drawDepth;
  if (!g_drawDepth)
    flush_frame_damage_locked();
  gfx::EndScreenDraw();
}

void present_locked(const URegion &region) noexcept {
  if (region.Empty())
    return;
  const auto &display = primary_display();
  if (!display)
    return;
  if (g_drawDepth) {
    for (uint32_t i = 0; i < region.count; ++i) add_frame_damage(region.rects[i]);
    return;
  }
  present_region(display, region);
}

void present_locked(const URect &r) noexcept {
  if (r.Empty())
    return;
  const auto &display = primary_display();
  if (!display)
    return;
  damage_locked(display, r);
}

void fill_screen_locked(const URect &r, uint32_t color) noexcept {
  const auto &display = primary_display();
  if (!display)
    return;
  const URect c = r.Intersect(bounds_of(*display));
  if (c.Empty())
    return;
  gfx::BeginScreenDraw();
  if (fill(display, c, color))
    damage_locked(display, c);
  gfx::EndScreenDraw();
}

URect screen_rect() noexcept {
  const auto &display = primary_display();
  return display ? bounds_of(*display) : URect{};
}

void invalidate_locked(KWindowObject &window, const URect &clientRect, bool erase) noexcept {
  const URect r = clientRect.Intersect(client_rect_locked(window));
  if (r.Empty())
    return;
  window.invalid = window.invalid.Union(r);
  window.erasePending = window.erasePending || erase;
  if (window.queue && effectively_visible_locked(window))
    wake_locked(*window.queue);
}

void expose_locked(const URect &screenRect) noexcept {
  SceneDrawGuard drawing;
  // A copy: draw_nonclient_locked and menus run arbitrary USER code.
  const auto display = primary_display();
  URegion desktop;
  desktop.Set(screenRect.Intersect(display ? bounds_of(*display) : URect{}));
  for (auto *w = topmost_locked(); w; w = w->below) {
    if (w->paintDC && w->process) {
      URegion clip;
      visible_region_locked(*w, clip);
      clip.Intersect(client_to_screen_locked(*w, w->painting));
      clip.Intersect(client_to_screen_locked(*w, client_rect_locked(*w)));
      (void) bind_dc_locked(w->process, *w, w->paintDC, w->paintClip, clip, nullptr);
    }
    rebind_window_dc_locked(*w);
    if (!effectively_visible_locked(*w))
      continue;
    URegion visible;
    visible_region_locked(*w, visible);
    const URect clientScreen = client_to_screen_locked(*w, client_rect_locked(*w));
    for (uint32_t i = 0; display && i < visible.count; ++i) {
      const URect exposed = visible.rects[i].Intersect(screenRect).Intersect(clientScreen);
      if (!exposed.Empty() && fill(display, exposed, w->background))
        damage_locked(display, exposed);
    }
    invalidate_locked(*w, screen_to_client_locked(*w, screenRect));
    if (!w->rect.Intersect(screenRect).Empty())
      draw_nonclient_locked(*w, screenRect);
    desktop.Subtract(clipped_rect_locked(*w));
  }
  if (!desktop.Empty()) {
    if (display) {
      for (uint32_t i = 0; i < desktop.count; ++i) {
        if (fill(display, desktop.rects[i], kDesktopColor))
          add_frame_damage(desktop.rects[i]);
      }
    }
  }
  draw_open_menu_locked(screenRect);
}

void visible_region_locked(const KWindowObject &window, URegion &out) noexcept {
  out.Set(clipped_rect_locked(window));
  for (const auto *w = topmost_locked(); w && w != &window; w = w->below) {
    if (effectively_visible_locked(*w))
      out.Subtract(clipped_rect_locked(*w));
  }
  subtract_open_menus_locked(out);
}

void abandon_paint_locked(KWindowObject &window) noexcept {
  if (!window.paintActive)
    return;
  window.invalid = window.invalid.Union(window.painting);
  window.erasePending = window.erasePending || window.paintingErase;
  if (window.process)
    gfx::DetachWindowDC(window.process, window.paintDC);
  window.paintDC = 0;
  window.paintActive = false;
  window.paintingErase = false;
  window.painting = {};
  window.paintErase = {};
  window.paintClip.Clear();
  if (g_activePaints) {
    --g_activePaints;
    gfx::EndScreenDraw();
  }
  if (window.queue && effectively_visible_locked(window))
    wake_locked(*window.queue);
}

void rebind_window_dc_locked(KWindowObject &window) noexcept {
  if (!window.windowDC || !window.process)
    return;
  (void) bind_window_clip_locked(window, nullptr);
}

void rebind_paint_dc_locked(KWindowObject &window) noexcept {
  if (!window.paintDC || !window.process)
    return;
  URegion clip;
  visible_region_locked(window, clip);
  if (effectively_visible_locked(window))
    clip.Intersect(client_to_screen_locked(window, window.painting));
  else
    clip.Clear();
  (void) bind_dc_locked(window.process, window, window.paintDC, window.paintClip, clip, nullptr);
}

void abandon_window_dc_locked(KWindowObject &window) noexcept {
  if (!window.windowDC)
    return;
  if (window.process)
    gfx::DetachWindowDC(window.process, window.windowDC);
  window.windowDC = 0;
  window.windowClip.Clear();
  if (g_activePaints) {
    --g_activePaints;
    gfx::EndScreenDraw();
  }
}

kstd::expected<PaintBegin, kstd::error_code> begin_paint_locked(KProcessObject &proc, KWindowObject &window, uint32_t reservedDC) noexcept {
  const auto display = primary_display();
  if (!display)
    return kstd::unexpected(KernelError::DeviceNotReady);
  if (window.paintDC) {
    if (gfx::DCExists(&proc, window.paintDC))
      return kstd::unexpected(KernelError::InvalidState);
    // The paint DC is gone without EndPaint. Recover its damage instead of
    // leaving the window permanently unable to begin another paint.
    abandon_paint_locked(window);
  }

  const HDC handle = reservedDC;
  const URect paint = window.invalid.Intersect(client_rect_locked(window));
  const bool erase = window.erasePending;
  URegion clip;
  visible_region_locked(window, clip);
  if (effectively_visible_locked(window))
    clip.Intersect(client_to_screen_locked(window, paint));
  else
    clip.Clear();
  if (const auto bound = bind_dc_locked(&proc, window, handle, window.paintClip, clip, display->surface.get()); bound)
    return kstd::unexpected(bound);

  window.invalid = {};
  window.erasePending = false;
  window.painting = paint;
  window.paintingErase = erase;
  window.paintDC = handle;
  window.paintActive = true;
  // Paint DC operations, including background erasure, write only to the
  // surface shadow. GFX keeps the cursor hidden until the last paint ends.
  ++g_activePaints;
  gfx::BeginScreenDraw();
  window.paintErase = {};
  bool erased = false;
  if (erase) {
    for (uint32_t i = 0; i < window.paintClip.count; ++i) {
      const URect &r = window.paintClip.rects[i];
      if (fill_shadow(display, r, window.background)) {
        window.paintErase = window.paintErase.Union(r);
        erased = true;
      }
    }
  }
  return PaintBegin{.dc = handle, .paint = paint, .erased = erased};
}

kstd::error_code end_paint_locked(KProcessObject &proc, KWindowObject &window, uint32_t hdc, DetachedDC &released) noexcept {
  if (window.process != &proc)
    return KernelError::AccessDenied;
  if (!hdc || window.paintDC != hdc || !window.paintActive)
    return KernelError::InvalidHandle;
  // The live clip can shrink after rasterization (hide, occlusion, or a scene
  // change). It is not a record of pixels already written into the shadow.
  // GFX returns its actual damage as it closes the slot.
  KClipRect drawn{};
  bool incomplete = false;
  if (const kstd::error_code closed = gfx::CloseWindowDC(&proc, hdc, drawn, incomplete, released); closed)
    return closed;
  URect damage = window.paintErase.Union({.left = drawn.left, .top = drawn.top, .right = drawn.right, .bottom = drawn.bottom});
  // A draw failed part way: the painted area needs another pass.
  const URect repaint = incomplete ? window.painting : URect{};
  window.paintDC = 0;
  window.paintActive = false;
  window.paintingErase = false;
  window.painting = {};
  window.paintErase = {};
  window.paintClip.Clear();
  if (g_activePaints) {
    --g_activePaints;
    gfx::EndScreenDraw();
  }
  damage = damage.Intersect(screen_rect());
  if (!damage.Empty())
    present_locked(damage);
  invalidate_locked(window, repaint, false);
  return {};
}

kstd::error_code bind_window_dc_locked(KProcessObject &proc, KWindowObject &window, HDC handle) noexcept {
  const auto display = primary_display();
  if (!display)
    return KernelError::DeviceNotReady;
  if (window.windowDC) {
    if (gfx::DCExists(&proc, window.windowDC))
      return KernelError::InvalidState;
    abandon_window_dc_locked(window);
  }
  window.windowDC = handle;
  if (const auto bound = bind_window_clip_locked(window, display->surface.get()); bound) {
    window.windowDC = 0;
    window.windowClip.Clear();
    return bound;
  }
  // Drawing goes to the surface shadow; keep the cursor out of it until release.
  ++g_activePaints;
  gfx::BeginScreenDraw();
  return {};
}

kstd::error_code release_window_dc_locked(KProcessObject &proc, KWindowObject &window, HDC dc, DetachedDC &released) noexcept {
  if (!dc || window.windowDC != dc)
    return KernelError::InvalidHandle;
  KClipRect drawn{};
  bool incomplete = false;
  if (const kstd::error_code closed = gfx::CloseWindowDC(&proc, dc, drawn, incomplete, released); closed)
    return closed;
  if (incomplete)
    invalidate_locked(window, client_rect_locked(window), false);
  URect damage{.left = drawn.left, .top = drawn.top, .right = drawn.right, .bottom = drawn.bottom};
  window.windowDC = 0;
  window.windowClip.Clear();
  if (g_activePaints) {
    --g_activePaints;
    gfx::EndScreenDraw();
  }
  damage = damage.Intersect(screen_rect());
  if (!damage.Empty())
    present_locked(damage);
  return {};
}

}// namespace kernel::user
