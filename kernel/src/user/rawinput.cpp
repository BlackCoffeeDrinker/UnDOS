#include "user/user.hpp"
#include <kernel/input.hpp>
#include <kernel/scheduler.hpp>
#include <kernel/gfx.hpp>
#include <kernel/kobject/KDisplay.hpp>

namespace kernel::user {

struct InputState {
  int32_t mouseX{0};
  int32_t mouseY{0};
  uint32_t buttons{0};
  
  uint32_t dragHwnd{0};
  int32_t dragOffX{0};
  int32_t dragOffY{0};
};

static InputState g_input;

static void draw_cursor(int32_t x, int32_t y) noexcept {
  // White crosshair with black outline
  Guard g;
  fill_screen_locked({x - 4, y - 1, x + 5, y + 2}, 0);
  fill_screen_locked({x - 1, y - 4, x + 2, y + 5}, 0);
  fill_screen_locked({x - 3, y, x + 4, y + 1}, 15);
  fill_screen_locked({x, y - 3, x + 1, y + 4}, 15);
}

static void erase_cursor(int32_t x, int32_t y) noexcept {
  URect r = {x - 4, y - 4, x + 5, y + 5};
  Guard g;
  expose_locked(r);
}

static void raw_input_thread(void *) {
  URect screen = screen_rect();
  g_input.mouseX = (screen.left + screen.right) / 2;
  g_input.mouseY = (screen.top + screen.bottom) / 2;
  
  draw_cursor(g_input.mouseX, g_input.mouseY);

  for (;;) {
    input::AnyInputEvent event;
    if (!KE_INPUT_WaitEvent(event, kstd::chrono::milliseconds(1000))) continue;
    
    if (event.base.is<input::MouseInputEvent>()) {
      auto &me = event.mouse;
      
      erase_cursor(g_input.mouseX, g_input.mouseY);
      
      g_input.mouseX += me.lastX;
      g_input.mouseY += me.lastY;
      
      if (g_input.mouseX < screen.left) g_input.mouseX = screen.left;
      if (g_input.mouseX >= screen.right) g_input.mouseX = screen.right - 1;
      if (g_input.mouseY < screen.top) g_input.mouseY = screen.top;
      if (g_input.mouseY >= screen.bottom) g_input.mouseY = screen.bottom - 1;
      
      draw_cursor(g_input.mouseX, g_input.mouseY);
      
      Guard g;
      KWindowObject *target = nullptr;
      
      if (g_input.dragHwnd) {
         target = lookup_locked(g_input.dragHwnd);
         if (target) {
            URect nr = target->rect;
            nr = nr.Offset(g_input.mouseX - (nr.left + g_input.dragOffX), g_input.mouseY - (nr.top + g_input.dragOffY));
            move_window_locked(*target, nr);
         }
      }

      if (me.buttonFlags != 0) {
          if (me.buttonFlags & input::mouse_button_flags::LeftDown) {
              target = window_from_point_locked(g_input.mouseX, g_input.mouseY);
              if (target) {
                  set_active_window_locked(target->hwnd);
                  HitTest ht = hit_test_locked(*target, g_input.mouseX, g_input.mouseY);
                  if (ht == HitTest::Caption) {
                      g_input.dragHwnd = target->hwnd;
                      g_input.dragOffX = g_input.mouseX - target->rect.left;
                      g_input.dragOffY = g_input.mouseY - target->rect.top;
                  } else if (ht == HitTest::Close) {
                      if (target->queue) post_locked(*target->queue, target->hwnd, kMsgClose, 0, 0);
                  } else if (ht == HitTest::Client) {
                      if (target->queue) {
                          URect cr = screen_to_client_locked(*target, {g_input.mouseX, g_input.mouseY, 0, 0});
                          post_locked(*target->queue, target->hwnd, kMsgLButtonDown, 0, (static_cast<uint32_t>(cr.top & 0xFFFF) << 16) | (static_cast<uint32_t>(cr.left) & 0xFFFF));
                      }
                  }
              } else {
                  set_active_window_locked(0);
              }
              g_input.buttons |= 1;
          } else if (me.buttonFlags & input::mouse_button_flags::LeftUp) {
              if (g_input.dragHwnd) g_input.dragHwnd = 0;
              target = window_from_point_locked(g_input.mouseX, g_input.mouseY);
              if (target && target->queue) {
                  URect cr = screen_to_client_locked(*target, {g_input.mouseX, g_input.mouseY, 0, 0});
                  post_locked(*target->queue, target->hwnd, kMsgLButtonUp, 0, (static_cast<uint32_t>(cr.top & 0xFFFF) << 16) | (static_cast<uint32_t>(cr.left) & 0xFFFF));
              }
              g_input.buttons &= ~1u;
          }
      }
      
      if (!g_input.dragHwnd) {
          target = window_from_point_locked(g_input.mouseX, g_input.mouseY);
          if (target && target->queue) {
              URect cr = screen_to_client_locked(*target, {g_input.mouseX, g_input.mouseY, 0, 0});
              post_locked(*target->queue, target->hwnd, kMsgMouseMove, 0, (static_cast<uint32_t>(cr.top & 0xFFFF) << 16) | (static_cast<uint32_t>(cr.left) & 0xFFFF));
          }
      }
    } else if (event.base.is<input::KeyboardInputEvent>()) {
       auto &ke = event.keyboard;
       Guard g;
       KWindowObject *active = active_window_locked();
       if (active && active->queue) {
           uint32_t msg = (ke.keyFlags & input::key_flags::Break) ? kMsgKeyUp : kMsgKeyDown;
           post_locked(*active->queue, active->hwnd, msg, ke.virtualKey, ke.scanCode);
           if (ke.character && !(ke.keyFlags & input::key_flags::Break)) {
               post_locked(*active->queue, active->hwnd, kMsgChar, static_cast<uint32_t>(static_cast<uint8_t>(ke.character)), 0);
           }
       }
    }
  }
}

void init_raw_input() noexcept {
    KE_SCHED_CreateThread(raw_input_thread, nullptr, 2);
}

} // namespace kernel::user
