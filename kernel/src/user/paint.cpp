#include "user/user.hpp"

#include <kernel/gfx.hpp>
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KGraphicsAdapter.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KSurface.hpp>

namespace kernel::user {
namespace {
static_assert(sizeof(URect) == sizeof(KClipRect), "URect and KClipRect must share a layout");

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
  if (adapter && adapter->FillRect && adapter->FillRect(display->adapter, display->surface, r.left, r.top, w, h, color)) return;
  KE_GFX_SoftwareFillRect(display->surface, r.left, r.top, w, h, color);
}

void present(const KObjectPtr<KDisplay> &display, const URect *rects, uint32_t count) noexcept {
  const auto &adapter = display->adapter;
  if (!adapter || !adapter->Present) return;
  adapter->Present(display->adapter, display->surface, reinterpret_cast<const KClipRect *>(rects), count);
}
}// namespace

void present_locked(const URegion &region) noexcept {
  if (region.Empty()) return;
  const auto display = primary_display();
  if (!display) return;
  if (region_merges_to_bounds(region)) {
    const URect bounds = region.Bounds();
    present(display, &bounds, 1);
    return;
  }
  present(display, region.rects, region.count);
}

void present_locked(const URect &r) noexcept {
  if (r.Empty()) return;
  const auto display = primary_display();
  if (!display) return;
  present(display, &r, 1);
}

void fill_screen_locked(const URect &r, uint32_t color) noexcept {
  const auto display = primary_display();
  if (!display) return;
  const URect c = r.Intersect(screen_rect());
  if (c.Empty()) return;
  fill(display, c, color);
  present(display, &c, 1);
}

URect screen_rect() noexcept {
  const auto display = primary_display();
  if (!display) return {};
  return {0, 0, static_cast<int32_t>(display->surface->width), static_cast<int32_t>(display->surface->height)};
}

void invalidate_locked(KWindowObject &window, const URect &windowRect) noexcept {
  // windowRect is relative to the frame; window.invalid is kept in client coordinates.
  const URect origin = client_to_screen_locked(window, {});
  const int32_t ox = origin.left - window.rect.left;
  const int32_t oy = origin.top - window.rect.top;
  const int32_t b = (window.style & kStyleBorder) ? kBorderWidth : 0;
  const URect clientInWindow{ox, oy, window.rect.right - window.rect.left - b, window.rect.bottom - window.rect.top - b};
  const URect r = windowRect.Intersect(clientInWindow).Offset(-ox, -oy);
  if (r.Empty()) return;
  window.invalid = window.invalid.Union(r);
  if (window.queue && window.Visible()) wake_locked(*window.queue);
}

void expose_locked(const URect &screenRect) noexcept {
  URegion desktop;
  desktop.Set(screenRect.Intersect(screen_rect()));
  for (auto *w = topmost_locked(); w; w = w->below) {
    if (!w->Visible()) continue;
    invalidate_locked(*w, screenRect.Offset(-w->rect.left, -w->rect.top));
    desktop.Subtract(w->rect);
  }
  if (desktop.Empty()) return;
  const auto display = primary_display();
  if (!display) return;
  for (uint32_t i = 0; i < desktop.count; ++i) fill(display, desktop.rects[i], kDesktopColor);
  present_locked(desktop);
}

void visible_region_locked(const KWindowObject &window, URegion &out) noexcept {
  out.Set(window.rect.Intersect(screen_rect()));
  for (auto *w = topmost_locked(); w && w != &window; w = w->below) {
    if (w->Visible()) out.Subtract(w->rect);
  }
}

bool begin_paint_locked(KProcessObject &proc, KWindowObject &window, uint32_t &hdc, URect &paint) noexcept {
  const auto display = primary_display();
  if (!display) return false;

  auto [slot, handle] = proc.dcTable.Allocate();
  if (!slot) return false;

  paint = window.invalid;
  window.invalid = {};

  visible_region_locked(window, window.paintClip);
  if (window.Visible()) {
    window.paintClip.Intersect(client_to_screen_locked(window, paint));
  } else {
    window.paintClip.Clear();
  }

  // Erase the background (the WM_ERASEBKGND default) before the app draws.
  for (uint32_t i = 0; i < window.paintClip.count; ++i) fill(display, window.paintClip.rects[i], window.background);

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
  return true;
}

}// namespace kernel::user
