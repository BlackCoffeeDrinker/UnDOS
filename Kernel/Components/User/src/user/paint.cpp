#include "gfx/gfx.hpp"
#include "user/user.hpp"

#include <kernel/gfx.hpp>
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KGraphicsAdapter.hpp>
#include <kernel/kobject/KMemoryBackingObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KSurface.hpp>

#include "gfx/bitmap_font.hpp"

namespace kernel::user {
namespace {
static_assert(sizeof(URect) == sizeof(KClipRect), "URect and KClipRect must share a layout");
struct Cursor {
  int32_t x{0}, y{0};
  bool enabled{false}, drawn{false};
  bool adapterSaved{false};
  URect savedRect{};
  uint8_t saved[9 * 9 * 4]{};
  uint32_t nativeSaved[9 * 9]{};
  KObjectPtr<KSurface> surface;
  KObjectPtr<KGraphicsAdapter> adapter;
} g_cursor;
uint32_t g_drawDepth{0};
uint32_t g_activePaints{0};

uint32_t pixel_bytes(const KSurface &surface) noexcept {
  switch (surface.pixelFormat) {
    case PixelFormat::INDEX8: return 1;
    case PixelFormat::RGB15:
    case PixelFormat::RGB16: return 2;
    case PixelFormat::RGB24: return 3;
    case PixelFormat::ARGB32: return 4;
  }
  return 0;
}

// The active target, refreshed only when the gfx topology generation moves.
// Callers hold the scene lock, which every target replacement also holds, so
// a reference stays valid for the duration of one USER operation. This keeps
// the driver lock and the target refcount off every draw.
KObjectPtr<KDisplay> g_display;
uint32_t g_displayGeneration{0};
const KObjectPtr<KDisplay> kNoDisplay;

const KObjectPtr<KDisplay> &primary_display() noexcept {
  const uint32_t generation = gfx::topology_generation();
  if (generation != g_displayGeneration) {
    g_display = gfx::active_video_present_target();
    g_displayGeneration = generation;
  }
  if (!g_display || !g_display->surface || !g_display->isAttachedToDesktop) return kNoDisplay;
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
  const auto &adapter = display->adapter;
  const int w = r.right - r.left;
  const int h = r.bottom - r.top;
  const uint32_t packed = gfx::Palette16ScreenColor(color, display->surface->bpp);
  const auto &surface = display->surface;
  if ((area(r) <= 256 || g_drawDepth) &&
      surface->backing && surface->backing->directAddress && surface->pitch > 0 &&
      static_cast<uint64_t>(surface->pitch) >= static_cast<uint64_t>(surface->width) * pixel_bytes(*surface) &&
      !KE_GFX_SoftwareFillRect(surface, r.left, r.top, w, h, packed)) return true;
  if (adapter && adapter->FillRect &&
      !adapter->FillRect(display->adapter, display->surface, r.left, r.top, w, h, packed))
    return !Any(adapter->caps & KGfxCaps::VisibleFill);
  // Last resort; scene painting has no caller to report a failed fill to.
  (void) KE_GFX_SoftwareFillRect(display->surface, r.left, r.top, w, h, packed);
  return true;
}

bool fill_shadow(const KObjectPtr<KDisplay> &display, const URect &r,
                 uint32_t color) noexcept {
  const uint32_t packed = gfx::Palette16ScreenColor(color, display->surface->bpp);
  return !KE_GFX_SoftwareFillRect(display->surface, r.left, r.top,
                                  r.right - r.left, r.bottom - r.top, packed);
}

void present(const KObjectPtr<KDisplay> &display, const URect *rects, uint32_t count) noexcept {
  const auto &adapter = display->adapter;
  if (!adapter || !adapter->Present) return;
  adapter->Present(display->adapter, display->surface, reinterpret_cast<const KClipRect *>(rects), count);
}

// Damage produced while a scene draw is open. It is presented once when the
// outermost scene draw ends instead of once per frame line or glyph span.
// Rectangles may overlap; a rectangle that adds little area to an existing
// entry is merged into it, and overflow collapses the list to its bounds.
URegion g_frameDamage;

void add_frame_damage(const URect &r) noexcept {
  if (r.Empty()) return;
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
    present(display, &bounds, 1);
    return;
  }
  present(display, region.rects, region.count);
}

// Presents now, or defers to the end of the open scene draw.
void damage_locked(const KObjectPtr<KDisplay> &display, const URect &r) noexcept {
  if (r.Empty()) return;
  if (g_drawDepth) {
    add_frame_damage(r);
    return;
  }
  present(display, &r, 1);
}

void flush_frame_damage_locked() noexcept {
  if (g_frameDamage.Empty()) return;
  URegion pending = g_frameDamage;
  g_frameDamage.Clear();
  const auto &display = primary_display();
  if (!display) return;
  // A mode change inside the scene can shrink the screen under the damage.
  pending.Intersect(bounds_of(*display));
  if (!pending.Empty()) present_region(display, pending);
}

// Restores the pixels under the cursor; presents them only when asked to.
void cursor_restore(bool presentNow) noexcept {
  const auto &surface = g_cursor.surface;
  const auto *r = &g_cursor.savedRect;
  if (g_cursor.adapterSaved) {
    // One FillRect per run of equal pixels in a row, not one per pixel.
    const int32_t width = r->right - r->left;
    uint32_t index = 0;
    for (int32_t y = r->top; y < r->bottom; ++y) {
      for (int32_t x = 0; x < width;) {
        const uint32_t color = g_cursor.nativeSaved[index + static_cast<uint32_t>(x)];
        int32_t end = x + 1;
        while (end < width && g_cursor.nativeSaved[index + static_cast<uint32_t>(end)] == color) ++end;
        (void) g_cursor.adapter->FillRect(g_cursor.adapter, surface, r->left + x, y, end - x, 1, color);
        x = end;
      }
      index += static_cast<uint32_t>(width);
    }
    g_cursor.drawn = false;
    if (presentNow && g_cursor.adapter->Present) {
      const KClipRect rect{.left = r->left, .top = r->top, .right = r->right, .bottom = r->bottom};
      g_cursor.adapter->Present(g_cursor.adapter, surface, &rect, 1);
    }
    return;
  }
  auto *base = surface->backing->directAddress.as_ptr<uint8_t>();
  const auto bytes = pixel_bytes(*surface);
  uint32_t index = 0;
  for (int32_t y = r->top; y < r->bottom; ++y)
    for (int32_t x = r->left; x < r->right; ++x)
      for (uint32_t b = 0; b < bytes; ++b)
        base[static_cast<size_t>(y) * static_cast<size_t>(surface->pitch) + static_cast<size_t>(x) * bytes + b] = g_cursor.saved[index++];
  g_cursor.drawn = false;
  if (!presentNow) return;
  const auto &display = primary_display();
  if (display && display->surface == surface) present(display, r, 1);
}

// Draws the cursor; `extra` (restored pixels not yet presented) joins its present.
void cursor_draw(const URect &extra) noexcept {
  const auto &display = primary_display();
  if (!g_cursor.enabled || g_cursor.drawn || g_drawDepth || g_activePaints || !display) {
    if (display && !extra.Empty()) present(display, &extra, 1);
    return;
  }
  const auto &surface = display->surface;
  const auto bytes = pixel_bytes(*surface);
  const URect screen = bounds_of(*display);
  const auto r = URect{.left = g_cursor.x - 4, .top = g_cursor.y - 4, .right = g_cursor.x + 5, .bottom = g_cursor.y + 5}.Intersect(screen);
  const URect damage = extra.Empty() ? r : r.Union(extra);
  if (r.Empty()) {
    if (!damage.Empty()) present(display, &damage, 1);
    return;
  }
  g_cursor.adapterSaved = false;
  const auto &adapter = display->adapter;
  const bool packed = bytes && surface->backing && surface->backing->directAddress &&
                      surface->pitch > 0 &&
                      static_cast<uint64_t>(surface->pitch) >= static_cast<uint64_t>(surface->width) * bytes &&
                      static_cast<uint64_t>(surface->pitch) * surface->height <= surface->sizeInBytes;
  if (packed) {
    const auto *base = surface->backing->directAddress.as_ptr<uint8_t>();
    uint32_t index = 0;
    for (int32_t y = r.top; y < r.bottom; ++y)
      for (int32_t x = r.left; x < r.right; ++x)
        for (uint32_t b = 0; b < bytes; ++b)
          g_cursor.saved[index++] = base[static_cast<size_t>(y) * static_cast<size_t>(surface->pitch) + static_cast<size_t>(x) * bytes + b];
  } else {
    const auto bail = [&] {
      if (!extra.Empty()) present(display, &extra, 1);
    };
    if (!adapter || !adapter->ReadPixel || !adapter->FillRect) return bail();
    uint32_t index = 0;
    for (int32_t y = r.top; y < r.bottom; ++y)
      for (int32_t x = r.left; x < r.right; ++x) {
        const auto pixel = adapter->ReadPixel(adapter, surface, x, y);
        if (!pixel) return bail();
        g_cursor.nativeSaved[index++] = *pixel;
      }
    g_cursor.adapterSaved = true;
  }
  g_cursor.surface = surface;
  g_cursor.adapter = adapter;
  g_cursor.savedRect = r;
  g_cursor.drawn = true;
  const URect shapes[] = {{.left = g_cursor.x - 4, .top = g_cursor.y - 1, .right = g_cursor.x + 5, .bottom = g_cursor.y + 2},
                          {.left = g_cursor.x - 1, .top = g_cursor.y - 4, .right = g_cursor.x + 2, .bottom = g_cursor.y + 5},
                          {.left = g_cursor.x - 3, .top = g_cursor.y, .right = g_cursor.x + 4, .bottom = g_cursor.y + 1},
                          {.left = g_cursor.x, .top = g_cursor.y - 3, .right = g_cursor.x + 1, .bottom = g_cursor.y + 4}};
  for (uint32_t i = 0; i < 4; ++i) {
    const auto c = shapes[i].Intersect(screen);
    if (!c.Empty()) fill(display, c, i < 2 ? 0 : 15);
  }
  present(display, &damage, 1);
}
}// namespace

void cursor_hide_locked() noexcept {
  if (!g_cursor.drawn || !g_cursor.surface || !g_cursor.surface->backing) return;
  cursor_restore(true);
}

void cursor_show_locked() noexcept { cursor_draw({}); }

URect cursor_exclude_locked(const KSurface *target, const URect &bounds) noexcept {
  if (!g_cursor.drawn || !g_cursor.surface || !g_cursor.surface->backing ||
      g_cursor.surface.get() != target || g_cursor.savedRect.Intersect(bounds).Empty())
    return {};
  const URect restored = g_cursor.savedRect;
  cursor_restore(false);
  return restored;
}

void cursor_include_locked(const URect &restored) noexcept {
  if (restored.Empty()) return;
  cursor_draw(restored);
}

void cursor_move_locked(int32_t x, int32_t y) noexcept {
  // One present covers both the old and the new cursor position.
  URect restored{};
  if (g_cursor.drawn && g_cursor.surface && g_cursor.surface->backing) {
    restored = g_cursor.savedRect;
    cursor_restore(false);
  }
  g_cursor.x = x;
  g_cursor.y = y;
  g_cursor.enabled = true;
  cursor_draw(restored);
}

void display_cache_reset_locked() noexcept {
  g_display = nullptr;
  g_displayGeneration = 0;
}

void cursor_reset_locked() noexcept {
  display_cache_reset_locked();
  g_cursor.drawn = false;
  g_cursor.adapterSaved = false;
  g_cursor.surface = nullptr;
  g_cursor.adapter = nullptr;
  g_cursor.savedRect = {};
}

uint32_t active_paint_count_locked() noexcept { return g_activePaints; }

void begin_scene_draw_locked() noexcept {
  cursor_hide_locked();
  ++g_drawDepth;
}

void end_scene_draw_locked() noexcept {
  if (g_drawDepth) --g_drawDepth;
  if (!g_drawDepth) flush_frame_damage_locked();
  cursor_show_locked();
}

void present_locked(const URegion &region) noexcept {
  if (region.Empty()) return;
  const auto &display = primary_display();
  if (!display) return;
  if (g_drawDepth) {
    for (uint32_t i = 0; i < region.count; ++i) add_frame_damage(region.rects[i]);
    return;
  }
  present_region(display, region);
  cursor_show_locked();
}

void present_locked(const URect &r) noexcept {
  if (r.Empty()) return;
  const auto &display = primary_display();
  if (!display) return;
  damage_locked(display, r);
  cursor_show_locked();
}

void fill_screen_locked(const URect &r, uint32_t color) noexcept {
  const auto &display = primary_display();
  if (!display) return;
  const URect c = r.Intersect(bounds_of(*display));
  if (c.Empty()) return;
  cursor_hide_locked();
  if (fill(display, c, color)) damage_locked(display, c);
  cursor_show_locked();
}

URect screen_rect() noexcept {
  const auto &display = primary_display();
  return display ? bounds_of(*display) : URect{};
}

void invalidate_locked(KWindowObject &window, const URect &clientRect, bool erase) noexcept {
  const URect r = clientRect.Intersect(client_rect_locked(window));
  if (r.Empty()) return;
  window.invalid = window.invalid.Union(r);
  window.erasePending = window.erasePending || erase;
  if (window.queue && effectively_visible_locked(window)) wake_locked(*window.queue);
}

void expose_locked(const URect &screenRect) noexcept {
  SceneDrawGuard drawing;
  // A copy: draw_nonclient_locked and menus run arbitrary USER code.
  const auto display = primary_display();
  URegion desktop;
  desktop.Set(screenRect.Intersect(display ? bounds_of(*display) : URect{}));
  for (auto *w = topmost_locked(); w; w = w->below) {
    if (w->paintDC && w->process) {
      auto *slot = gfx::FindDC(w->process, w->paintDC);
      if (slot) {
        visible_region_locked(*w, w->paintClip);
        w->paintClip.Intersect(client_to_screen_locked(*w, w->painting));
        w->paintClip.Intersect(client_to_screen_locked(*w, client_rect_locked(*w)));
        const auto origin = client_to_screen_locked(*w, {});
        (void) slot->ChangeRenderState();
        slot->clipCount = w->paintClip.count;
        slot->originX = origin.left;
        slot->originY = origin.top;
      }
    }
    rebind_window_dc_locked(*w);
    if (!effectively_visible_locked(*w)) continue;
    URegion visible;
    visible_region_locked(*w, visible);
    const URect clientScreen = client_to_screen_locked(*w, client_rect_locked(*w));
    for (uint32_t i = 0; display && i < visible.count; ++i) {
      const URect exposed = visible.rects[i].Intersect(screenRect).Intersect(clientScreen);
      if (!exposed.Empty() && fill(display, exposed, w->background))
        damage_locked(display, exposed);
    }
    invalidate_locked(*w, screen_to_client_locked(*w, screenRect));
    if (!w->rect.Intersect(screenRect).Empty()) draw_nonclient_locked(*w, screenRect);
    desktop.Subtract(clipped_rect_locked(*w));
  }
  if (!desktop.Empty()) {
    if (display) {
      cursor_hide_locked();
      for (uint32_t i = 0; i < desktop.count; ++i) {
        if (fill(display, desktop.rects[i], kDesktopColor)) add_frame_damage(desktop.rects[i]);
      }
    }
  }
  draw_open_menu_locked(screenRect);
}

void visible_region_locked(const KWindowObject &window, URegion &out) noexcept {
  out.Set(clipped_rect_locked(window));
  for (const auto *w = topmost_locked(); w && w != &window; w = w->below) {
    if (effectively_visible_locked(*w)) out.Subtract(clipped_rect_locked(*w));
  }
  subtract_open_menus_locked(out);
}

void abandon_paint_locked(KWindowObject &window) noexcept {
  if (!window.paintActive) return;
  window.invalid = window.invalid.Union(window.painting);
  window.erasePending = window.erasePending || window.paintingErase;
  window.paintDC = 0;
  window.paintActive = false;
  window.paintingErase = false;
  window.painting = {};
  window.paintErase = {};
  window.paintClip.Clear();
  if (g_activePaints) --g_activePaints;
  if (window.queue && effectively_visible_locked(window)) wake_locked(*window.queue);
}

void rebind_window_dc_locked(KWindowObject &window) noexcept {
  if (!window.windowDC || !window.process) return;
  auto *slot = gfx::FindDC(window.process, window.windowDC);
  if (!slot) return;
  visible_region_locked(window, window.windowClip);
  if (effectively_visible_locked(window))
    window.windowClip.Intersect(client_to_screen_locked(window, client_rect_locked(window)));
  else
    window.windowClip.Clear();
  const URect origin = client_to_screen_locked(window, {});
  (void) slot->ChangeRenderState();
  slot->originX = origin.left;
  slot->originY = origin.top;
  slot->clipRects = reinterpret_cast<const KClipRect *>(window.windowClip.rects);
  slot->clipCount = window.windowClip.count;
  slot->clipped = true;
  slot->hwnd = window.hwnd;
}

void abandon_window_dc_locked(KWindowObject &window) noexcept {
  if (!window.windowDC) return;
  window.windowDC = 0;
  window.windowClip.Clear();
  if (g_activePaints) --g_activePaints;
  cursor_show_locked();
}

kstd::expected<PaintBegin, kstd::error_code> begin_paint_locked(KProcessObject &proc, KWindowObject &window, uint32_t reservedDC) noexcept {
  const auto display = primary_display();
  if (!display) return kstd::unexpected(KernelError::DeviceNotReady);
  if (window.paintDC) {
    if (gfx::FindDC(&proc, window.paintDC)) return kstd::unexpected(KernelError::InvalidState);
    // A caller can explicitly DeleteDC a paint DC. Recover its damage instead
    // of leaving the window permanently unable to begin another paint.
    abandon_paint_locked(window);
  }

  auto *slot = gfx::FindDC(&proc, reservedDC);
  if (!slot) return kstd::unexpected(KernelError::InvalidHandle);
  const HDC handle = reservedDC;

  const URect paint = window.invalid.Intersect(client_rect_locked(window));
  const bool erase = window.erasePending;
  window.invalid = {};
  window.erasePending = false;
  window.painting = paint;
  window.paintingErase = erase;

  visible_region_locked(window, window.paintClip);
  if (effectively_visible_locked(window)) {
    window.paintClip.Intersect(client_to_screen_locked(window, paint));
  } else {
    window.paintClip.Clear();
  }

  (void) slot->ChangeRenderState();
  slot->target = display->surface;
  const URect origin = client_to_screen_locked(window, {});
  slot->originX = origin.left;
  slot->originY = origin.top;
  slot->clipped = true;
  slot->noAccel = true;
  slot->clipRects = reinterpret_cast<const KClipRect *>(window.paintClip.rects);
  slot->clipCount = window.paintClip.count;
  slot->hwnd = window.hwnd;

  window.paintDC = handle;
  window.paintActive = true;
  ++g_activePaints;

  // Paint DC operations, including background erasure, write only to the
  // surface shadow. Keep the global cursor hidden until the last paint ends.
  cursor_hide_locked();
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
  if (window.process != &proc) return KernelError::AccessDenied;
  if (!hdc || window.paintDC != hdc || !window.paintActive) return KernelError::InvalidHandle;
  auto *slot = gfx::FindDC(&proc, hdc);
  if (!slot) return KernelError::InvalidHandle;
  // The live clip can shrink after rasterization (hide, occlusion, or a scene
  // change). It is not a record of pixels already written into the shadow.
  // Preserve GFX's actual damage before Close resets the slot.
  URect damage = window.paintErase;
  if (slot->presentationDirty) {
    const auto &r = slot->presentationBounds;
    damage = damage.Union({.left = r.left, .top = r.top, .right = r.right, .bottom = r.bottom});
  }
  if (const kstd::error_code closed = gfx::CloseDC(&proc, hdc, released); closed)
    return closed;
  window.paintDC = 0;
  window.paintActive = false;
  window.paintingErase = false;
  window.painting = {};
  window.paintErase = {};
  window.paintClip.Clear();
  if (g_activePaints) --g_activePaints;
  damage = damage.Intersect(screen_rect());
  if (!damage.Empty()) present_locked(damage);
  else
    cursor_show_locked();
  return {};
}

kstd::error_code bind_window_dc_locked(KProcessObject &proc, KWindowObject &window, HDC handle) noexcept {
  const auto display = primary_display();
  if (!display) return KernelError::DeviceNotReady;
  if (window.windowDC) {
    if (gfx::FindDC(&proc, window.windowDC)) return KernelError::InvalidState;
    abandon_window_dc_locked(window);
  }
  auto *slot = gfx::FindDC(&proc, handle);
  if (!slot) return KernelError::InvalidHandle;
  slot->target = display->surface;
  slot->noAccel = true;
  window.windowDC = handle;
  rebind_window_dc_locked(window);
  // Drawing goes to the surface shadow; keep the cursor out of it until release.
  ++g_activePaints;
  cursor_hide_locked();
  return {};
}

kstd::error_code release_window_dc_locked(KProcessObject &proc, KWindowObject &window, HDC dc, DetachedDC &released) noexcept {
  if (!dc || window.windowDC != dc) return KernelError::InvalidHandle;
  auto *slot = gfx::FindDC(&proc, dc);
  if (!slot) return KernelError::InvalidHandle;
  URect damage{};
  if (slot->presentationDirty) {
    const auto &r = slot->presentationBounds;
    damage = {.left = r.left, .top = r.top, .right = r.right, .bottom = r.bottom};
  }
  if (const kstd::error_code closed = gfx::CloseDC(&proc, dc, released); closed) return closed;
  window.windowDC = 0;
  window.windowClip.Clear();
  if (g_activePaints) --g_activePaints;
  damage = damage.Intersect(screen_rect());
  if (!damage.Empty()) present_locked(damage);
  else
    cursor_show_locked();
  return {};
}

}// namespace kernel::user
