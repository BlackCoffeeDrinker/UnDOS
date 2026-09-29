#include "user/user.hpp"
#include <kernel/gfx.hpp>
#include <kernel/kobject/KDisplay.hpp>

namespace kernel::user {

URect client_to_screen_locked(const KWindowObject &window, const URect &clientRect) noexcept {
  int32_t ox = 0;
  int32_t oy = 0;
  if (window.style & kStyleBorder) {
    ox += kBorderWidth;
    oy += kBorderWidth;
  }
  if (window.style & kStyleCaption) oy += kCaptionHeight;
  
  return clientRect.Offset(window.rect.left + ox, window.rect.top + oy);
}

URect screen_to_client_locked(const KWindowObject &window, const URect &screenRect) noexcept {
  int32_t ox = 0;
  int32_t oy = 0;
  if (window.style & kStyleBorder) {
    ox += kBorderWidth;
    oy += kBorderWidth;
  }
  if (window.style & kStyleCaption) oy += kCaptionHeight;
  
  return screenRect.Offset(-(window.rect.left + ox), -(window.rect.top + oy));
}

HitTest hit_test_locked(const KWindowObject &window, int32_t x, int32_t y) noexcept {
  if (x < window.rect.left || x >= window.rect.right || y < window.rect.top || y >= window.rect.bottom)
    return HitTest::Nowhere;

  const int32_t b = (window.style & kStyleBorder) ? kBorderWidth : 0;
  const int32_t c = (window.style & kStyleCaption) ? kCaptionHeight : 0;

  if (x < window.rect.left + b) {
    if (y < window.rect.top + b) return HitTest::TopLeft;
    if (y >= window.rect.bottom - b) return HitTest::BottomLeft;
    return HitTest::Left;
  }
  if (x >= window.rect.right - b) {
    if (y < window.rect.top + b) return HitTest::TopRight;
    if (y >= window.rect.bottom - b) return HitTest::BottomRight;
    return HitTest::Right;
  }
  if (y < window.rect.top + b) return HitTest::Top;
  if (y >= window.rect.bottom - b) return HitTest::Bottom;

  if (c > 0 && y < window.rect.top + b + c) {
    if (x >= window.rect.right - b - kCaptionHeight + 2) return HitTest::Close;
    return HitTest::Caption;
  }

  return HitTest::Client;
}

void draw_nonclient_locked(KWindowObject &window) noexcept {
  if (!window.Visible()) return;

  URegion clip;
  visible_region_locked(window, clip);
  if (clip.Empty()) return;

  KObjectPtr<KDisplay> display = KE_GFX_GetPrimaryDisplay();
  if (!display) return;
  KSurface *surf = display->surface.get();

  auto draw_rect = [&](int32_t l, int32_t t, int32_t w, int32_t h, uint32_t color) {
    URect r = {l, t, l + w, t + h};
    for (uint32_t i = 0; i < clip.count; ++i) {
      URect intersection = r.Intersect(clip.rects[i]);
      if (!intersection.Empty()) {
        KE_GFX_SoftwareFillRect(surf, intersection.left, intersection.top, 
                                intersection.right - intersection.left, 
                                intersection.bottom - intersection.top, color);
      }
    }
  };

  const URect &r = window.rect;
  
  // Outer frame
  draw_rect(r.left, r.top, r.right - r.left, 1, kWindowFrameColor);
  draw_rect(r.left, r.bottom - 1, r.right - r.left, 1, kWindowFrameColor);
  draw_rect(r.left, r.top, 1, r.bottom - r.top, kWindowFrameColor);
  draw_rect(r.right - 1, r.top, 1, r.bottom - r.top, kWindowFrameColor);

  // Border
  if (window.style & kStyleBorder) {
    draw_rect(r.left + 1, r.top + 1, r.right - r.left - 2, kBorderWidth - 1, kWindowFaceColor);
    draw_rect(r.left + 1, r.bottom - kBorderWidth, r.right - r.left - 2, kBorderWidth - 1, kWindowFaceColor);
    draw_rect(r.left + 1, r.top + 1, kBorderWidth - 1, r.bottom - r.top - 2, kWindowFaceColor);
    draw_rect(r.right - kBorderWidth, r.top + 1, kBorderWidth - 1, r.bottom - r.top - 2, kWindowFaceColor);
  }

  // Caption
  if (window.style & kStyleCaption) {
    bool active = (active_window_locked() == &window);
    uint32_t color = active ? kActiveCaptionColor : kInactiveCaptionColor;
    int32_t cy = r.top + (window.style & kStyleBorder ? kBorderWidth : 0);
    int32_t cx = r.left + (window.style & kStyleBorder ? kBorderWidth : 0);
    int32_t cw = r.right - r.left - (window.style & kStyleBorder ? 2 * kBorderWidth : 0);
    draw_rect(cx, cy, cw, kCaptionHeight, color);
    
    // Close button
    draw_rect(cx + cw - kCaptionHeight + 2, cy + 2, kCaptionHeight - 4, kCaptionHeight - 4, kWindowFaceColor);
  }
}

} // namespace kernel::user
