#include "user/user.hpp"

#include <kernel/gfx.hpp>
#include <kernel/kobject/KDisplay.hpp>

#include "user/bitmap_font.hpp"

namespace kernel::user {

URect client_rect_locked(const KWindowObject &window) noexcept {
  if (window.state == KWindowObject::State::Minimized) return {};
  const int32_t b = (window.style & kStyleBorder) ? kBorderWidth : 0;
  const int32_t c = (window.style & kStyleCaption) ? kCaptionHeight : 0;
  const int32_t width = window.rect.right - window.rect.left - 2 * b;
  const int32_t height = window.rect.bottom - window.rect.top - 2 * b - c;
  return {.left = 0, .top = 0, .right = width > 0 ? width : 0, .bottom = height > 0 ? height : 0};
}

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
  if (window.state == KWindowObject::State::Minimized && window.parent) return HitTest::Caption;

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
    if (x < window.rect.left + b + kCaptionHeight) return HitTest::SysMenu;
    if (x >= window.rect.right - b - kCaptionHeight) return HitTest::MaxButton;
    if (x >= window.rect.right - b - 2 * kCaptionHeight) return HitTest::MinButton;
    return HitTest::Caption;
  }

  return HitTest::Client;
}

void draw_nonclient_locked(KWindowObject &window) noexcept {
  if (!effectively_visible_locked(window)) return;
  SceneDrawGuard drawing;

  URegion clip;
  visible_region_locked(window, clip);
  if (clip.Empty()) return;

  auto draw_rect = [&](int32_t l, int32_t t, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    const URect r = {.left = l, .top = t, .right = l + w, .bottom = t + h};
    for (uint32_t i = 0; i < clip.count; ++i) {
      if (URect intersection = r.Intersect(clip.rects[i]); !intersection.Empty()) fill_screen_locked(intersection, color);
    }
  };

  const URect &r = window.rect;
  if (window.state == KWindowObject::State::Minimized && window.parent) {
    const bool selected = focused_window_locked() == &window;
    const int32_t width = r.right - r.left;
    const int32_t iconX = r.left + (width - 32) / 2, iconY = r.top + 2;
    draw_rect(r.left, r.top, width, r.bottom - r.top, kWindowFaceColor);
    draw_rect(iconX, iconY, 32, 30, 0);
    draw_rect(iconX + 1, iconY + 1, 30, 28, 15);
    draw_rect(iconX + 3, iconY + 3, 26, 5, kActiveCaptionColor);
    draw_rect(iconX + 3, iconY + 10, 26, 17, kWindowFaceColor);
    draw_rect(iconX + 6, iconY + 13, 7, 6, 1);
    draw_rect(iconX + 17, iconY + 13, 7, 6, 4);
    constexpr auto cell = static_cast<int32_t>(gfx::kFontCellWidth);
    size_t length = 0;
    while (length < window.title.size() && window.title[length] &&
           static_cast<int64_t>(length + 1) * cell <= width - 4) ++length;
    const int32_t labelWidth = static_cast<int32_t>(length) * cell;
    const int32_t labelX = r.left + (width - labelWidth) / 2;
    draw_rect(labelX - 1, r.top + 36, labelWidth + 2, 16, selected ? kActiveCaptionColor : kWindowFaceColor);
    gfx::DrawBitmapText(labelX, r.top + 36, window.title.data(), length,
                        selected ? 15 : 0, kWindowFaceColor, false,
                        [&](int64_t l, int64_t t, int64_t right, int64_t bottom, uint32_t ink) {
                          draw_rect(static_cast<int32_t>(l), static_cast<int32_t>(t),
                                    static_cast<int32_t>(right - l), static_cast<int32_t>(bottom - t), ink);
                        });
    return;
  }

  // Border
  if (window.style & kStyleBorder) {
    draw_rect(r.left, r.top, r.right - r.left, 1, kWindowFrameColor);
    draw_rect(r.left, r.bottom - 1, r.right - r.left, 1, kWindowFrameColor);
    draw_rect(r.left, r.top, 1, r.bottom - r.top, kWindowFrameColor);
    draw_rect(r.right - 1, r.top, 1, r.bottom - r.top, kWindowFrameColor);
    draw_rect(r.left + 1, r.top + 1, r.right - r.left - 2, 1, 15);
    draw_rect(r.left + 1, r.top + 1, 1, r.bottom - r.top - 2, 15);
    draw_rect(r.left + 2, r.bottom - 2, r.right - r.left - 3, 1, 8);
    draw_rect(r.right - 2, r.top + 2, 1, r.bottom - r.top - 3, 8);
    draw_rect(r.left + 2, r.top + 2, r.right - r.left - 4, 1, kWindowFaceColor);
    draw_rect(r.left + 2, r.top + 2, 1, r.bottom - r.top - 4, kWindowFaceColor);
    draw_rect(r.left + 2, r.bottom - 3, r.right - r.left - 4, 1, kWindowFaceColor);
    draw_rect(r.right - 3, r.top + 2, 1, r.bottom - r.top - 4, kWindowFaceColor);
  }

  // Caption
  if (window.style & kStyleCaption) {
    const auto *focus = focused_window_locked();
    const bool active = (active_window_locked() == &window || focus == &window);
    const uint32_t color = active ? kActiveCaptionColor : kInactiveCaptionColor;
    const int32_t cy = r.top + (window.style & kStyleBorder ? kBorderWidth : 0);
    const int32_t cx = r.left + (window.style & kStyleBorder ? kBorderWidth : 0);
    const int32_t cw = r.right - r.left - (window.style & kStyleBorder ? 2 * kBorderWidth : 0);
    draw_rect(cx, cy, cw, kCaptionHeight, color);
    draw_rect(cx, cy + kCaptionHeight - 1, cw, 1, kWindowFrameColor);

    auto button = [&](int32_t x) {
      draw_rect(x, cy, kCaptionHeight, kCaptionHeight, kWindowFrameColor);
      draw_rect(x + 1, cy + 1, kCaptionHeight - 2, kCaptionHeight - 2, kWindowFaceColor);
      draw_rect(x + 1, cy + 1, kCaptionHeight - 2, 1, 15);
      draw_rect(x + 1, cy + 1, 1, kCaptionHeight - 2, 15);
      draw_rect(x + kCaptionHeight - 2, cy + 2, 1, kCaptionHeight - 3, 8);
      draw_rect(x + 2, cy + kCaptionHeight - 2, kCaptionHeight - 3, 1, 8);
    };
    if (cw >= 3 * kCaptionHeight) {
      button(cx);
      button(cx + cw - 2 * kCaptionHeight);
      button(cx + cw - kCaptionHeight);
      draw_rect(cx + 4, cy + 6, 10, 5, 0);
      draw_rect(cx + 5, cy + 7, 8, 2, 15);
      const int32_t minx = cx + cw - 2 * kCaptionHeight;
      for (int32_t i = 0; i < 4; ++i) draw_rect(minx + 5 + i, cy + 7 + i, 8 - 2 * i, 1, 0);
      const int32_t maxx = cx + cw - kCaptionHeight;
      for (int32_t i = 0; i < 4; ++i) {
        draw_rect(maxx + 5 + i, cy + (window.state == KWindowObject::State::Maximized ? 6 - i : 10 - i), 8 - 2 * i, 1, 0);
        if (window.state == KWindowObject::State::Maximized) draw_rect(maxx + 5 + i, cy + 10 + i, 8 - 2 * i, 1, 0);
      }
    }
    size_t length = 0;
    const int32_t available = cw - 3 * kCaptionHeight - 5;
    while (length < window.title.size() && window.title[length] &&
           static_cast<int64_t>(length + 1) * gfx::kFontCellWidth <= available) ++length;
    const int32_t titleWidth = static_cast<int32_t>(length) * static_cast<int32_t>(gfx::kFontCellWidth) + 1;
    const int32_t titleLeft = cx + kCaptionHeight + 2;
    const int32_t titleRight = cx + cw - 2 * kCaptionHeight - 2;
    int32_t titleX = cx + (cw - titleWidth) / 2;
    if (titleX + titleWidth > titleRight) titleX = titleRight - titleWidth;
    if (titleX < titleLeft) titleX = titleLeft;
    gfx::DrawBitmapText(titleX, cy,
                        window.title.data(), length,
                        kCaptionTextColor, color, false,
                        [&](int64_t l, int64_t t, int64_t right, int64_t bottom, uint32_t ink) {
                          draw_rect(static_cast<int32_t>(l), static_cast<int32_t>(t),
                                    static_cast<int32_t>(right - l) + 1, static_cast<int32_t>(bottom - t), ink);
                        });
  }
}

}// namespace kernel::user
