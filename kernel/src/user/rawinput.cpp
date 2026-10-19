#include "user/user.hpp"
#include <kernel/input.hpp>
#include <kernel/scheduler.hpp>

namespace kernel::user {
namespace {
struct InputState {
  int32_t mouseX{0}, mouseY{0};
  uint32_t buttons{0}, modifiers{0};
  uint32_t dragHwnd{0};
  HitTest dragHit{HitTest::Nowhere};
  int32_t dragX{0}, dragY{0};
  URect dragRect{};
  uint32_t lastClickHwnd{0}, lastClickTime{0};
  int32_t lastClickX{0}, lastClickY{0};
  HitTest lastClickHit{HitTest::Nowhere};
} g_input;

uint32_t packed_point(const KWindowObject &window) noexcept {
  const auto point = screen_to_client_locked(window, {g_input.mouseX, g_input.mouseY, 0, 0});
  return (static_cast<uint32_t>(point.top & 0xFFFF) << 16) |
         static_cast<uint32_t>(point.left & 0xFFFF);
}

void post_mouse(KWindowObject &window, uint32_t message) noexcept {
  if (window.queue)
    post_locked(*window.queue, window.hwnd, message,
                g_input.buttons | ((g_input.modifiers & 0x1FF) << 16), packed_point(window));
}

bool resize_hit(HitTest hit) noexcept {
  return hit >= HitTest::Left && hit <= HitTest::BottomRight;
}

void drag_locked() noexcept {
  auto *window = lookup_locked(g_input.dragHwnd);
  if (!window || !effectively_visible_locked(*window) || window->state != 0) {
    g_input.dragHwnd = 0;
    return;
  }
  auto rect = g_input.dragRect;
  const int32_t dx = g_input.mouseX - g_input.dragX, dy = g_input.mouseY - g_input.dragY;
  const auto hit = g_input.dragHit;
  if (hit == HitTest::Caption) rect = rect.Offset(dx, dy);
  else {
    if (hit == HitTest::Left || hit == HitTest::TopLeft || hit == HitTest::BottomLeft) rect.left += dx;
    if (hit == HitTest::Right || hit == HitTest::TopRight || hit == HitTest::BottomRight) rect.right += dx;
    if (hit == HitTest::Top || hit == HitTest::TopLeft || hit == HitTest::TopRight) rect.top += dy;
    if (hit == HitTest::Bottom || hit == HitTest::BottomLeft || hit == HitTest::BottomRight) rect.bottom += dy;
    const int32_t minWidth = 4 * kCaptionHeight, minHeight = 2 * kBorderWidth + kCaptionHeight + 16;
    if (rect.right - rect.left < minWidth) {
      if (hit == HitTest::Left || hit == HitTest::TopLeft || hit == HitTest::BottomLeft) rect.left = rect.right - minWidth;
      else rect.right = rect.left + minWidth;
    }
    if (rect.bottom - rect.top < minHeight) {
      if (hit == HitTest::Top || hit == HitTest::TopLeft || hit == HitTest::TopRight) rect.top = rect.bottom - minHeight;
      else rect.bottom = rect.top + minHeight;
    }
  }
  // Interactive child movement stays within the parent's client. The syscall
  // also permits partially clipped children for intentional layouts.
  if (auto *parent = lookup_locked(window->parent)) {
    const auto bounds = client_to_screen_locked(*parent, client_rect_locked(*parent));
    if (g_input.dragHit == HitTest::Caption) {
      if (rect.left < bounds.left) rect = rect.Offset(bounds.left - rect.left, 0);
      if (rect.top < bounds.top) rect = rect.Offset(0, bounds.top - rect.top);
      if (rect.right > bounds.right) rect = rect.Offset(bounds.right - rect.right, 0);
      if (rect.bottom > bounds.bottom) rect = rect.Offset(0, bounds.bottom - rect.bottom);
    } else rect = rect.Intersect(bounds);
  }
  if (!rect.Empty() && rect.left >= -32768 && rect.top >= -32768 &&
      rect.right <= 32767 && rect.bottom <= 32767 && move_window_locked(*window, rect))
    window->restoredRect = rect;
}

void mouse_locked(const input::MouseInputEvent &event) noexcept {
  const auto screen = screen_rect();
  if (screen.Empty()) return;
  const int64_t x = event.isMovementAbsolute ? event.lastX : static_cast<int64_t>(g_input.mouseX) + event.lastX;
  const int64_t y = event.isMovementAbsolute ? event.lastY : static_cast<int64_t>(g_input.mouseY) + event.lastY;
  g_input.mouseX = static_cast<int32_t>(x < screen.left ? screen.left : x >= screen.right ? screen.right - 1 : x);
  g_input.mouseY = static_cast<int32_t>(y < screen.top ? screen.top : y >= screen.bottom ? screen.bottom - 1 : y);
  cursor_hide_locked();
  if (g_input.dragHwnd) drag_locked();

  auto *captured = lookup_locked(capture_locked());
  auto *target = mouse_target_locked(g_input.mouseX, g_input.mouseY);
  const bool dragging = g_input.dragHwnd != 0;
  if (event.buttonFlags & input::mouse_button_flags::LeftDown) {
    g_input.buttons |= 1;
    if (captured) post_mouse(*captured, kMsgLButtonDown);
    else if (target) {
      const auto hit = hit_test_locked(*target, g_input.mouseX, g_input.mouseY);
      const auto time = static_cast<uint32_t>(event.timestamp.count() / 1000000);
      const int32_t dx = g_input.mouseX - g_input.lastClickX, dy = g_input.mouseY - g_input.lastClickY;
      const bool doubleClick = g_input.lastClickHwnd == target->hwnd && g_input.lastClickHit == hit &&
                               time - g_input.lastClickTime <= 500 && dx >= -4 && dx <= 4 && dy >= -4 && dy <= 4;
      g_input.lastClickHwnd = doubleClick ? 0 : target->hwnd;
      g_input.lastClickTime = time;
      g_input.lastClickX = g_input.mouseX;
      g_input.lastClickY = g_input.mouseY;
      g_input.lastClickHit = hit;
      if (target->state != 2) set_focus_locked(target->hwnd);
      if (target->state == 2) {
        if (doubleClick || !target->parent) show_command_locked(*target, 4);
        else set_focus_locked(target->hwnd);
      } else if (hit == HitTest::Client) {
        post_mouse(*target, doubleClick ? kMsgLButtonDoubleClick : kMsgLButtonDown);
      } else if (hit == HitTest::SysMenu) {
        if (doubleClick && target->queue) post_locked(*target->queue, target->hwnd, kMsgClose, 0, 0);
        else if (target->queue)
          post_locked(*target->queue, target->hwnd, 0x00A1, static_cast<uint32_t>(HitTest::SysMenu),
                      (static_cast<uint32_t>(g_input.mouseY & 0xFFFF) << 16) |
                      static_cast<uint32_t>(g_input.mouseX & 0xFFFF));
      } else if (hit == HitTest::MinButton) show_command_locked(*target, 2);
      else if (hit == HitTest::MaxButton || (hit == HitTest::Caption && doubleClick))
        show_command_locked(*target, target->state == 3 ? 4 : 3);
      else if (target->state == 0 && (hit == HitTest::Caption || resize_hit(hit))) {
        g_input.dragHwnd = target->hwnd;
        g_input.dragHit = hit;
        g_input.dragX = g_input.mouseX;
        g_input.dragY = g_input.mouseY;
        g_input.dragRect = target->rect;
      }
    } else {
      set_focus_locked(0);
      set_active_window_locked(0);
    }
  }
  if (event.buttonFlags & input::mouse_button_flags::LeftUp) {
    g_input.buttons &= ~1u;
    if (!g_input.dragHwnd && target && (captured || hit_test_locked(*target, g_input.mouseX, g_input.mouseY) == HitTest::Client))
      post_mouse(*target, kMsgLButtonUp);
    g_input.dragHwnd = 0;
  }
  if (event.buttonFlags & input::mouse_button_flags::RightDown) {
    g_input.buttons |= 2;
    if (target && (captured || hit_test_locked(*target, g_input.mouseX, g_input.mouseY) == HitTest::Client))
      post_mouse(*target, kMsgRButtonDown);
  }
  if (event.buttonFlags & input::mouse_button_flags::RightUp) {
    g_input.buttons &= ~2u;
    if (target && (captured || hit_test_locked(*target, g_input.mouseX, g_input.mouseY) == HitTest::Client))
      post_mouse(*target, kMsgRButtonUp);
  }
  if (!dragging && !g_input.dragHwnd && target && (event.lastX || event.lastY) &&
      (captured || hit_test_locked(*target, g_input.mouseX, g_input.mouseY) == HitTest::Client))
    post_mouse(*target, kMsgMouseMove);
  cursor_move_locked(g_input.mouseX, g_input.mouseY);
}

void keyboard_locked(const input::KeyboardInputEvent &event) noexcept {
  g_input.modifiers = event.modifiers;
  const bool released = (event.keyFlags & input::key_flags::Break) != 0;
  const bool control = (event.modifiers & input::modifier_flags::Control) != 0;
  const bool alt = (event.modifiers & input::modifier_flags::Alt) != 0;
  const bool shift = (event.modifiers & input::modifier_flags::Shift) != 0;
  if (!released && !(event.keyFlags & input::key_flags::Repeat)) {
    if ((event.virtualKey == 0x09 && (alt || control)) || (event.virtualKey == 0x75 && control)) {
      cycle_window_locked(control && !alt, shift); // Alt+Tab roots, Ctrl+Tab/F6 groups.
      return;
    }
    if (alt && event.virtualKey == 0x73) { // Alt+F4
      if (auto *w = focused_window_locked(); w && w->queue) post_locked(*w->queue, w->hwnd, kMsgClose, 0, 0);
      return;
    }
    if (alt && event.virtualKey == 0x20) { // Alt+Space opens the focused frame's system menu.
      auto *window = focused_window_locked();
      if (!window) window = active_window_locked();
      if (window && window->state == 2 && window->parent && !released && event.virtualKey == 0x0D) {
        show_command_locked(*window, 4);
        return;
      }
      if (window && window->queue && effectively_visible_locked(*window)) {
        const int32_t border = (window->style & kStyleBorder) ? kBorderWidth : 0;
        const int32_t x = window->rect.left + border;
        const int32_t y = window->rect.top + border + kCaptionHeight;
        post_locked(*window->queue, window->hwnd, 0x00A1, static_cast<uint32_t>(HitTest::SysMenu),
                    (static_cast<uint32_t>(y & 0xFFFF) << 16) | static_cast<uint32_t>(x & 0xFFFF));
      }
      return;
    }
  }
  auto *window = focused_window_locked();
  if (!window) window = active_window_locked();
  if (!window || !window->queue || !effectively_visible_locked(*window) || window->state == 2) return;
  const uint32_t metadata = event.scanCode | ((event.keyFlags & 7u) << 8) | ((event.modifiers & 0x1FF) << 16);
  post_locked(*window->queue, window->hwnd, released ? kMsgKeyUp : kMsgKeyDown, event.virtualKey, metadata);
  if (event.character && !released && !alt && !control)
    post_locked(*window->queue, window->hwnd, kMsgChar, static_cast<uint8_t>(event.character), metadata);
}

void raw_input_thread(void *) {
  {
    Guard guard;
    const auto screen = screen_rect();
    g_input.mouseX = (screen.left + screen.right) / 2;
    g_input.mouseY = (screen.top + screen.bottom) / 2;
    cursor_move_locked(g_input.mouseX, g_input.mouseY);
  }
  for (;;) {
    input::AnyInputEvent event;
    if (!KE_INPUT_WaitEvent(event, kstd::chrono::milliseconds(1000))) continue;
    Guard guard;
    if (event.base.is<input::MouseInputEvent>()) mouse_locked(event.mouse);
    else if (event.base.is<input::KeyboardInputEvent>()) keyboard_locked(event.keyboard);
  }
}
} // namespace

void init_raw_input() noexcept {
  if (!KE_SCHED_CreateThread(raw_input_thread, nullptr, 2)) KE_Print("USER: cannot create raw input worker");
}
} // namespace kernel::user
