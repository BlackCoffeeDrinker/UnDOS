#include "user/user.hpp"

#include <kernel/gfx.hpp>
#include <kernel/kobject/KDisplay.hpp>

#include "user/bitmap_font.hpp"
#include "user/ui_text.hpp"

namespace kernel::user {

int32_t caption_height_locked() noexcept {
  const int32_t metricHeight = UiText::current().text_height() + 4;
  return metricHeight > kCaptionButtonSize ? metricHeight : kCaptionButtonSize;
}

int32_t minimized_child_height_locked() noexcept {
  return 40 + UiText::current().text_height();
}

URect client_rect_locked(const KWindowObject &window) noexcept {
  if (window.state == KWindowObject::State::Minimized) return {};
  const int32_t b = (window.style & kStyleBorder) ? kBorderWidth : 0;
  const int32_t c = (window.style & kStyleCaption) ? caption_height_locked() : 0;
  const int32_t m = window.menu ? menu_row_height_locked() : 0;
  const int32_t width = window.rect.right - window.rect.left - 2 * b;
  const int32_t height = window.rect.bottom - window.rect.top - 2 * b - c - m;
  return {.left = 0, .top = 0, .right = width > 0 ? width : 0, .bottom = height > 0 ? height : 0};
}

URect client_to_screen_locked(const KWindowObject &window, const URect &clientRect) noexcept {
  int32_t ox = 0;
  int32_t oy = 0;
  if (window.style & kStyleBorder) {
    ox += kBorderWidth;
    oy += kBorderWidth;
  }
  if (window.style & kStyleCaption) oy += caption_height_locked();
  if (window.menu) oy += menu_row_height_locked();

  return clientRect.Offset(window.rect.left + ox, window.rect.top + oy);
}

URect screen_to_client_locked(const KWindowObject &window, const URect &screenRect) noexcept {
  int32_t ox = 0;
  int32_t oy = 0;
  if (window.style & kStyleBorder) {
    ox += kBorderWidth;
    oy += kBorderWidth;
  }
  if (window.style & kStyleCaption) oy += caption_height_locked();
  if (window.menu) oy += menu_row_height_locked();

  return screenRect.Offset(-(window.rect.left + ox), -(window.rect.top + oy));
}

HitTest hit_test_locked(const KWindowObject &window, int32_t x, int32_t y) noexcept {
  if (x < window.rect.left || x >= window.rect.right || y < window.rect.top || y >= window.rect.bottom)
    return HitTest::Nowhere;
  if (window.state == KWindowObject::State::Minimized && window.parent) return HitTest::Caption;

  const int32_t b = (window.style & kStyleBorder) ? kBorderWidth : 0;
  const int32_t c = (window.style & kStyleCaption) ? caption_height_locked() : 0;

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
    const int32_t buttonTop = window.rect.top + b + (c - kCaptionButtonSize) / 2;
    if (x < window.rect.left + b + kCaptionButtonSize && y >= buttonTop &&
        y < buttonTop + kCaptionButtonSize) return HitTest::SysMenu;
    if (x >= window.rect.right - b - kCaptionButtonSize && y >= buttonTop &&
        y < buttonTop + kCaptionButtonSize) return HitTest::MaxButton;
    if (x >= window.rect.right - b - 2 * kCaptionButtonSize && y >= buttonTop &&
        y < buttonTop + kCaptionButtonSize) return HitTest::MinButton;
    return HitTest::Caption;
  }

  if (window.menu &&
      y < window.rect.top + b + c + menu_row_height_locked())
    return HitTest::MenuBar;

  return HitTest::Client;
}

void draw_nonclient_locked(KWindowObject &window) noexcept {
  draw_nonclient_locked(window, window.rect);
}

void draw_nonclient_locked(KWindowObject &window, const URect &limit) noexcept {
  if (!effectively_visible_locked(window)) return;
  SceneDrawGuard drawing;

  URegion clip;
  visible_region_locked(window, clip);
  clip.Intersect(limit);
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
    const UiText ui = UiText::current();
    const int32_t labelHeight = ui.text_height();
    const int32_t labelTop = r.top + 36;
    const size_t length = ui.fit(window.title.data(), window.title.size(), false, width - 4);
    const int32_t labelWidth = ui.width(window.title.data(), length, false);
    const int32_t labelX = r.left + (width - labelWidth) / 2;
    draw_rect(labelX - 1, labelTop, labelWidth + 2, labelHeight + 4,
              selected ? kActiveCaptionColor : kWindowFaceColor);
    ui.draw(labelX, labelTop + 2, window.title.data(), length, false, selected ? 15 : 0, kWindowFaceColor, false,
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
    const int32_t captionHeight = caption_height_locked();
    const int32_t buttonTop = cy + (captionHeight - kCaptionButtonSize) / 2;
    draw_rect(cx, cy, cw, captionHeight, color);
    draw_rect(cx, cy + captionHeight - 1, cw, 1, kWindowFrameColor);

    auto button = [&](int32_t x) {
      draw_rect(x, buttonTop, kCaptionButtonSize, kCaptionButtonSize, kWindowFrameColor);
      draw_rect(x + 1, buttonTop + 1, kCaptionButtonSize - 2, kCaptionButtonSize - 2, kWindowFaceColor);
      draw_rect(x + 1, buttonTop + 1, kCaptionButtonSize - 2, 1, 15);
      draw_rect(x + 1, buttonTop + 1, 1, kCaptionButtonSize - 2, 15);
      draw_rect(x + kCaptionButtonSize - 2, buttonTop + 2, 1, kCaptionButtonSize - 3, 8);
      draw_rect(x + 2, buttonTop + kCaptionButtonSize - 2, kCaptionButtonSize - 3, 1, 8);
    };
    if (cw >= 3 * kCaptionButtonSize) {
      button(cx);
      button(cx + cw - 2 * kCaptionButtonSize);
      button(cx + cw - kCaptionButtonSize);
      draw_rect(cx + 4, buttonTop + 6, 10, 5, 0);
      draw_rect(cx + 5, buttonTop + 7, 8, 2, 15);
      const int32_t minx = cx + cw - 2 * kCaptionButtonSize;
      for (int32_t i = 0; i < 4; ++i) draw_rect(minx + 5 + i, buttonTop + 7 + i, 8 - 2 * i, 1, 0);
      const int32_t maxx = cx + cw - kCaptionButtonSize;
      for (int32_t i = 0; i < 4; ++i) {
        draw_rect(maxx + 5 + i, buttonTop + (window.state == KWindowObject::State::Maximized ? 6 - i : 10 - i), 8 - 2 * i, 1, 0);
        if (window.state == KWindowObject::State::Maximized) draw_rect(maxx + 5 + i, buttonTop + 10 + i, 8 - 2 * i, 1, 0);
      }

    }
    const UiText ui = UiText::current();
    const int32_t available = cw - 3 * kCaptionButtonSize - 5;
    const size_t length = ui.fit(window.title.data(), window.title.size(), false, available);
    const int32_t titleWidth = ui.width(window.title.data(), length, false) + 1;
    const int32_t titleLeft = cx + kCaptionButtonSize + 2;
    const int32_t titleRight = cx + cw - 2 * kCaptionButtonSize - 2;
    int32_t titleX = cx + (cw - titleWidth) / 2;
    if (titleX + titleWidth > titleRight) titleX = titleRight - titleWidth;
    if (titleX < titleLeft) titleX = titleLeft;
    const int32_t titleY = cy + (captionHeight - ui.text_height()) / 2;
    ui.draw(titleX, titleY, window.title.data(), length, false, kCaptionTextColor, color, false,
            [&](int64_t l, int64_t t, int64_t right, int64_t bottom, uint32_t ink) {
              draw_rect(static_cast<int32_t>(l), static_cast<int32_t>(t),
                        static_cast<int32_t>(right - l), static_cast<int32_t>(bottom - t), ink);
            });
  }

  if (window.menu) {
    const int32_t border = (window.style & kStyleBorder) ? kBorderWidth : 0;
    const int32_t caption = (window.style & kStyleCaption) ? caption_height_locked() : 0;
    const int32_t menuTop = r.top + border + caption;
    const int32_t menuLeft = r.left + border;
    const int32_t menuWidth = r.right - r.left - 2 * border;
    const int32_t menuHeight = menu_row_height_locked();
    draw_rect(menuLeft, menuTop, menuWidth, menuHeight, kWindowFaceColor);
    draw_rect(menuLeft, menuTop + menuHeight - 1, menuWidth, 1, 8);
    const UiText ui = menu_text_locked();
    int32_t x = menuLeft;
    for (uint32_t i = 0; i < menu_item_count_locked(window.menu); ++i) {
      MenuItemView item{};
      if (!menu_item_locked(window.menu, i, item)) break;
      const int32_t width = item.text.empty() ? 8 : ui.width(item.text.data(), item.text.size(), false) + 16;
      if (x + width > menuLeft + menuWidth) break;
      const uint32_t foreground = item.flags & (kMenuItemDisabled | kMenuItemGrayed) ? 8 : 0;
      if (item.flags & kMenuItemSeparator) {
        draw_rect(x + width / 2, menuTop + 4, 1, menuHeight > 8 ? menuHeight - 8 : 1, 8);
      } else {
        ui.draw(x + 8, menuTop + 2, item.text.data(), item.text.size(), false, foreground,
                kWindowFaceColor, true,
                [&](int64_t left, int64_t top, int64_t right, int64_t bottom, uint32_t color) {
                  draw_rect(static_cast<int32_t>(left), static_cast<int32_t>(top),
                            static_cast<int32_t>(right - left), static_cast<int32_t>(bottom - top), color);
                });
      }
      x += width;
    }
  }
}

void repaint_all() noexcept {
  Guard guard;
  expose_locked(screen_rect());
}

}// namespace kernel::user
