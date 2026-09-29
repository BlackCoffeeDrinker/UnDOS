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
  if (adapter && adapter->FillRect && adapter->FillRect(display->surface, r.left, r.top, w, h, color)) return;
  KE_GFX_SoftwareFillRect(display->surface, r.left, r.top, w, h, color);
}
}// namespace

URect screen_rect() noexcept {
  const auto display = primary_display();
  if (!display) return {};
  return {0, 0, static_cast<int32_t>(display->surface->width), static_cast<int32_t>(display->surface->height)};
}

void invalidate_locked(KWindowObject &window, const URect &windowRect) noexcept {
  const URect client{0, 0, window.rect.right - window.rect.left, window.rect.bottom - window.rect.top};
  const URect r = windowRect.Intersect(client);
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
    window.paintClip.Intersect(paint.Offset(window.rect.left, window.rect.top));
  } else {
    window.paintClip.Clear();
  }

  // Erase the background (the WM_ERASEBKGND default) before the app draws.
  for (uint32_t i = 0; i < window.paintClip.count; ++i) fill(display, window.paintClip.rects[i], window.background);

  slot->lock.lock();
  slot->display = display;
  slot->target = display->surface;
  slot->originX = window.rect.left;
  slot->originY = window.rect.top;
  slot->clipped = true;
  slot->clipRects = reinterpret_cast<const KClipRect *>(window.paintClip.rects);
  slot->clipCount = window.paintClip.count;
  slot->clipOwner = KObjectPtr<KObject>(&window);
  slot->lock.unlock();

  hdc = handle;
  return true;
}

}// namespace kernel::user
