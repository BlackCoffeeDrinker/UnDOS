#include "user/user.hpp"

#include <kernel/gfx.hpp>
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KGraphicsAdapter.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/kobject/KMemoryBackingObject.hpp>
#include <kernel/gfx/bitmap_font.hpp>

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

KObjectPtr<KDisplay> primary_display() noexcept {
  auto display = KE_GFX_GetPrimaryDisplay();
  if (display && !display->surface) display->surface = KE_GFX_CreateSurfaceForMode(display, 0);
  if (!display || !display->surface) return nullptr;
  return display;
}

void fill(const KObjectPtr<KDisplay> &display, const URect &r, uint32_t color) noexcept {
  const auto &adapter = display->adapter;
  const int w = r.right - r.left;
  const int h = r.bottom - r.top;
  const uint32_t packed = gfx::Palette16ScreenColor(color, display->surface->bpp);
  const auto &surface = display->surface;
  if (static_cast<uint64_t>(w) * static_cast<uint64_t>(h) <= 256 &&
      surface->backing && surface->backing->directAddress && surface->pitch > 0 &&
      static_cast<uint64_t>(surface->pitch) >= static_cast<uint64_t>(surface->width) * pixel_bytes(*surface) &&
      KE_GFX_SoftwareFillRect(surface, r.left, r.top, w, h, packed)) return;
  if (adapter && adapter->FillRect && adapter->FillRect(display->adapter, display->surface, r.left, r.top, w, h, packed)) return;
  KE_GFX_SoftwareFillRect(display->surface, r.left, r.top, w, h, packed);
}

void present(const KObjectPtr<KDisplay> &display, const URect *rects, uint32_t count) noexcept {
  const auto &adapter = display->adapter;
  if (!adapter || !adapter->Present) return;
  adapter->Present(display->adapter, display->surface, reinterpret_cast<const KClipRect *>(rects), count);
}
}// namespace

void cursor_hide_locked() noexcept {
  if (!g_cursor.drawn || !g_cursor.surface || !g_cursor.surface->backing) return;
  const auto surface = g_cursor.surface;
  const auto *r = &g_cursor.savedRect;
  if (g_cursor.adapterSaved) {
    uint32_t index = 0;
    for (int32_t y = r->top; y < r->bottom; ++y)
      for (int32_t x = r->left; x < r->right; ++x)
        g_cursor.adapter->FillRect(g_cursor.adapter, surface, x, y, 1, 1, g_cursor.nativeSaved[index++]);
    g_cursor.drawn = false;
    if (g_cursor.adapter->Present) {
      const KClipRect rect{r->left, r->top, r->right, r->bottom};
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
  const auto display = primary_display();
  if (display && display->surface == surface) present(display, r, 1);
}

void cursor_show_locked() noexcept {
  if (!g_cursor.enabled || g_cursor.drawn || g_drawDepth) return;
  const auto display = primary_display();
  if (!display) return;
  const auto surface = display->surface;
  const auto bytes = pixel_bytes(*surface);
  const auto r = URect{g_cursor.x - 4, g_cursor.y - 4, g_cursor.x + 5, g_cursor.y + 5}.Intersect(screen_rect());
  if (r.Empty()) return;
  g_cursor.adapterSaved = false;
  const auto &adapter = display->adapter;
  const bool packed = bytes && surface->backing && surface->backing->directAddress &&
                      surface->pitch > 0 &&
                      static_cast<uint64_t>(surface->pitch) >= static_cast<uint64_t>(surface->width) * bytes &&
                      static_cast<uint64_t>(surface->pitch) * surface->height <= surface->sizeInBytes;
  if (packed) {
    auto *base = surface->backing->directAddress.as_ptr<uint8_t>();
    uint32_t index = 0;
    for (int32_t y = r.top; y < r.bottom; ++y)
      for (int32_t x = r.left; x < r.right; ++x)
        for (uint32_t b = 0; b < bytes; ++b)
          g_cursor.saved[index++] = base[static_cast<size_t>(y) * static_cast<size_t>(surface->pitch) + static_cast<size_t>(x) * bytes + b];
  } else {
    if (!adapter || !adapter->ReadPixel || !adapter->FillRect) return;
    uint32_t index = 0;
    for (int32_t y = r.top; y < r.bottom; ++y)
      for (int32_t x = r.left; x < r.right; ++x)
        if (!adapter->ReadPixel(adapter, surface, x, y, g_cursor.nativeSaved[index++])) return;
    g_cursor.adapterSaved = true;
  }
  g_cursor.surface = surface;
  g_cursor.adapter = adapter;
  g_cursor.savedRect = r;
  g_cursor.drawn = true;
  const URect shapes[] = {{g_cursor.x - 4, g_cursor.y - 1, g_cursor.x + 5, g_cursor.y + 2},
                          {g_cursor.x - 1, g_cursor.y - 4, g_cursor.x + 2, g_cursor.y + 5},
                          {g_cursor.x - 3, g_cursor.y, g_cursor.x + 4, g_cursor.y + 1},
                          {g_cursor.x, g_cursor.y - 3, g_cursor.x + 1, g_cursor.y + 4}};
  for (uint32_t i = 0; i < 4; ++i) {
    const auto c = shapes[i].Intersect(screen_rect());
    if (!c.Empty()) fill(display, c, i < 2 ? 0 : 15);
  }
  present(display, &r, 1);
}

void cursor_move_locked(int32_t x, int32_t y) noexcept {
  cursor_hide_locked();
  g_cursor.x = x;
  g_cursor.y = y;
  g_cursor.enabled = true;
  cursor_show_locked();
}

void begin_scene_draw_locked() noexcept {
  cursor_hide_locked();
  ++g_drawDepth;
}

void end_scene_draw_locked() noexcept {
  if (g_drawDepth) --g_drawDepth;
  cursor_show_locked();
}

void present_locked(const URegion &region) noexcept {
  if (region.Empty()) return;
  const auto display = primary_display();
  if (!display) return;
  if (region_merges_to_bounds(region)) {
    const URect bounds = region.Bounds();
    present(display, &bounds, 1);
    cursor_show_locked();
    return;
  }
  present(display, region.rects, region.count);
  cursor_show_locked();
}

void present_locked(const URect &r) noexcept {
  if (r.Empty()) return;
  const auto display = primary_display();
  if (!display) return;
  present(display, &r, 1);
  cursor_show_locked();
}

void fill_screen_locked(const URect &r, uint32_t color) noexcept {
  const auto display = primary_display();
  if (!display) return;
  const URect c = r.Intersect(screen_rect());
  if (c.Empty()) return;
  cursor_hide_locked();
  fill(display, c, color);
  present(display, &c, 1);
  cursor_show_locked();
}

URect screen_rect() noexcept {
  const auto display = primary_display();
  if (!display) return {};
  return {0, 0, static_cast<int32_t>(display->surface->width), static_cast<int32_t>(display->surface->height)};
}

void invalidate_locked(KWindowObject &window, const URect &clientRect) noexcept {
  const URect r = clientRect.Intersect(client_rect_locked(window));
  if (r.Empty()) return;
  window.invalid = window.invalid.Union(r);
  if (window.queue && effectively_visible_locked(window)) wake_locked(*window.queue);
}

void expose_locked(const URect &screenRect) noexcept {
  SceneDrawGuard drawing;
  URegion desktop;
  desktop.Set(screenRect.Intersect(screen_rect()));
  for (auto *w = topmost_locked(); w; w = w->below) {
    if (w->paintDC && w->process) {
      auto *slot = w->process->dcTable.Resolve(w->paintDC);
      if (slot) {
        visible_region_locked(*w, w->paintClip);
        w->paintClip.Intersect(client_to_screen_locked(*w, w->painting));
        w->paintClip.Intersect(client_to_screen_locked(*w, client_rect_locked(*w)));
        const auto origin = client_to_screen_locked(*w, {});
        slot->lock.lock();
        slot->clipCount = w->paintClip.count;
        slot->originX = origin.left;
        slot->originY = origin.top;
        slot->lock.unlock();
      }
    }
    if (!effectively_visible_locked(*w)) continue;
    invalidate_locked(*w, screen_to_client_locked(*w, screenRect));
    if (!w->rect.Intersect(screenRect).Empty()) draw_nonclient_locked(*w);
    desktop.Subtract(clipped_rect_locked(*w));
  }
  if (desktop.Empty()) return;
  const auto display = primary_display();
  if (!display) return;
  cursor_hide_locked();
  for (uint32_t i = 0; i < desktop.count; ++i) fill(display, desktop.rects[i], kDesktopColor);
  present_locked(desktop);
}

void visible_region_locked(const KWindowObject &window, URegion &out) noexcept {
  out.Set(clipped_rect_locked(window));
  for (auto *w = topmost_locked(); w && w != &window; w = w->below) {
    if (effectively_visible_locked(*w)) out.Subtract(clipped_rect_locked(*w));
  }
}

bool begin_paint_locked(KProcessObject &proc, KWindowObject &window, uint32_t &hdc, URect &paint) noexcept {
  const auto display = primary_display();
  if (!display) return false;
  if (window.paintDC) {
    if (proc.dcTable.Resolve(window.paintDC)) return false;
    // A caller can explicitly DeleteDC a paint DC. Recover its damage instead
    // of leaving the window permanently unable to begin another paint.
    window.invalid = window.invalid.Union(window.painting);
    window.paintDC = 0;
    window.painting = {};
    window.paintClip.Clear();
  }

  auto [slot, handle] = proc.dcTable.Allocate();
  if (!slot) return false;

  paint = window.invalid.Intersect(client_rect_locked(window));
  window.invalid = {};
  window.painting = paint;

  visible_region_locked(window, window.paintClip);
  if (effectively_visible_locked(window)) {
    window.paintClip.Intersect(client_to_screen_locked(window, paint));
  } else {
    window.paintClip.Clear();
  }

  // Erase the background (the WM_ERASEBKGND default) before the app draws.
  cursor_hide_locked();
  for (uint32_t i = 0; i < window.paintClip.count; ++i) fill(display, window.paintClip.rects[i], window.background);
  cursor_show_locked();

  slot->lock.lock();
  slot->display = display;
  slot->target = display->surface;
  const URect origin = client_to_screen_locked(window, {});
  slot->originX = origin.left;
  slot->originY = origin.top;
  slot->clipped = true;
  slot->clipRects = reinterpret_cast<const KClipRect *>(window.paintClip.rects);
  slot->clipCount = window.paintClip.count;
  slot->clipOwner = KObjectPtr<KObject>(&window);
  slot->lock.unlock();

  hdc = handle;
  window.paintDC = handle;
  return true;
}

bool end_paint_locked(KProcessObject &proc, KWindowObject &window, uint32_t hdc) noexcept {
  if (!hdc || window.paintDC != hdc || window.process != &proc) return false;
  auto *slot = proc.dcTable.Resolve(hdc);
  if (!slot) return false;
  // The live clip can shrink after rasterization (hide, occlusion, or a scene
  // change). It is not a record of pixels already written into the shadow.
  // Preserve GFX's actual damage before Close resets the slot.
  URect damage = window.paintClip.Bounds();
  slot->lock.lock();
  if (slot->presentationDirty) {
    const auto &r = slot->presentationBounds;
    damage = damage.Union({r.left, r.top, r.right, r.bottom});
  }
  slot->lock.unlock();
  if (!proc.dcTable.Close(hdc)) return false;
  present_locked(damage.Intersect(screen_rect()));
  window.paintDC = 0;
  window.painting = {};
  window.paintClip.Clear();
  return true;
}

}// namespace kernel::user
