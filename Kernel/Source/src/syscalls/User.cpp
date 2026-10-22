#include "stdkrn.hpp"
#include "syscalls.hpp"
#include "user/user.hpp"

namespace {
using namespace kernel::user;

URect to_urect(const Rect &rect) {
  return {.left = rect.left, .top = rect.top, .right = rect.right, .bottom = rect.bottom};
}

Rect to_rect(const URect &rect) {
  return {.left = rect.left, .top = rect.top, .right = rect.right, .bottom = rect.bottom};
}

void to_msg(const QueuedMessage &message, Msg &out) {
  out.hwnd = message.hwnd;
  out.message = message.message;
  out.wParam = message.wParam;
  out.lParam = message.lParam;
  out.time = message.time;
}
}// namespace

kstd::error_code handle_RegisterClass_V1(__KRN_USER_REGISTERCLASS_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return register_class(**process, args.name, args.wndProc, args.background);
}

kstd::error_code handle_CreateWindow_V1(__KRN_USER_CREATEWINDOW_V1 &&args) {
  args.hwnd = 0;
  const auto process = caller_process();
  if (!process) return process.error();
  const auto window = create_window(**process, {
      .className = args.className,
      .title = args.title,
      .style = static_cast<uint32_t>(args.style),
      .parent = args.parent,
      .x = args.x,
      .y = args.y,
      .width = args.width,
      .height = args.height,
      .userData = args.userData});
  if (!window) return window.error();
  args.hwnd = *window;
  return {};
}

kstd::error_code handle_DestroyWindow_V1(__KRN_USER_DESTROYWINDOW_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return destroy_window(**process, args.hwnd);
}

kstd::error_code handle_ShowWindow_V1(__KRN_USER_SHOWWINDOW_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return show_window(**process, args.hwnd, args.command);
}

kstd::error_code handle_MoveWindow_V1(__KRN_USER_MOVEWINDOW_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return move_window(**process, args.hwnd, args.x, args.y, args.width, args.height);
}

kstd::error_code handle_GetWindowRect_V1(__KRN_USER_GETWINDOWRECT_V1 &&args) {
  args.rect = {};
  const auto rect = get_window_rect(args.hwnd);
  if (!rect) return rect.error();
  args.rect = to_rect(*rect);
  return {};
}

kstd::error_code handle_InvalidateRect_V1(__KRN_USER_INVALIDATERECT_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  const auto rect = to_urect(args.rect);
  return invalidate_rect(**process, args.hwnd, args.whole ? nullptr : &rect);
}

kstd::error_code handle_BeginPaint_V1(__KRN_USER_BEGINPAINT_V1 &&args) {
  args.dc = 0;
  args.paint = {};
  const auto process = caller_process();
  if (!process) return process.error();
  const auto paint = begin_paint(**process, args.hwnd);
  if (!paint) return paint.error();
  args.dc = paint->dc;
  args.paint.rcPaint = to_rect(paint->paint);
  args.paint.erased = 1;
  return {};
}

kstd::error_code handle_EndPaint_V1(__KRN_USER_ENDPAINT_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return end_paint(**process, args.hwnd, args.dc);
}

kstd::error_code handle_GetMessage_V1(__KRN_USER_GETMESSAGE_V1 &&args) {
  args.msg = {};
  const auto message = get_message(true, true);
  if (!message) return message.error();
  to_msg(*message, args.msg);
  return {};
}

kstd::error_code handle_PeekMessage_V1(__KRN_USER_PEEKMESSAGE_V1 &&args) {
  args.msg = {};
  const auto message = get_message(false, args.remove != 0);
  if (!message) {
    // The existing ABI uses TIMEOUT for an empty nonblocking poll.
    return message.error() == kernel::KernelError::WouldBlock ? kernel::KernelError::Timeout : message.error();
  }
  to_msg(*message, args.msg);
  return {};
}

kstd::error_code handle_PostMessage_V1(__KRN_USER_POSTMESSAGE_V1 &&args) {
  return post_message(args.hwnd, args.message, args.wParam, args.lParam);
}

kstd::error_code handle_GetWindowLongPtr_V1(__KRN_USER_GETWINDOWLONGPTR_V1 &&args) {
  args.value = 0;
  const auto value = get_window_long(args.hwnd, args.index);
  if (!value) return value.error();
  args.value = *value;
  return {};
}

kstd::error_code handle_SetWindowLongPtr_V1(__KRN_USER_SETWINDOWLONGPTR_V1 &&args) {
  args.previous = 0;
  const auto process = caller_process();
  if (!process) return process.error();
  const auto previous = set_window_long(**process, args.hwnd, args.index, args.value);
  if (!previous) return previous.error();
  args.previous = *previous;
  return {};
}

kstd::error_code handle_SetCapture_V1(__KRN_USER_SETCAPTURE_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return set_capture(**process, args.hwnd);
}

kstd::error_code handle_GetClientRect_V1(__KRN_USER_GETCLIENTRECT_V1 &&args) {
  args.rect = {};
  const auto rect = get_client_rect(args.hwnd);
  if (!rect) return rect.error();
  args.rect = to_rect(*rect);
  return {};
}

kstd::error_code handle_SetFocus_V1(__KRN_USER_SETFOCUS_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return set_focus(**process, args.hwnd);
}

kstd::error_code handle_SetWindowText_V1(__KRN_USER_SETWINDOWTEXT_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return set_window_text(**process, args.hwnd, args.text);
}

kstd::error_code handle_CreateMenu_V1(__KRN_USER_CREATEMENU_V1 &&args) {
  args.menu = 0;
  if (args.popup > 1) return kernel::KernelError::InvalidArgument;
  const auto process = caller_process();
  if (!process) return process.error();
  const auto menu = create_menu(**process, args.popup != 0);
  if (!menu) return menu.error();
  args.menu = *menu;
  return {};
}

kstd::error_code handle_DestroyMenu_V1(__KRN_USER_DESTROYMENU_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return destroy_menu(**process, args.menu);
}

kstd::error_code handle_AppendMenu_V1(__KRN_USER_APPENDMENU_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return append_menu(**process, args.menu, static_cast<uint32_t>(args.flags),
                     args.item, args.text);
}

kstd::error_code handle_SetMenu_V1(__KRN_USER_SETMENU_V1 &&args) {
  const auto process = caller_process();
  if (!process) return process.error();
  return set_menu(**process, args.hwnd, args.menu);
}

kstd::error_code handle_GetMenu_V1(__KRN_USER_GETMENU_V1 &&args) {
  args.menu = 0;
  const auto menu = get_menu(args.hwnd);
  if (!menu) return menu.error();
  args.menu = *menu;
  return {};
}

kstd::error_code handle_ReleaseCapture_V1(__KRN_USER_RELEASECAPTURE_V1 &&args) {
  (void) args;
  const auto process = caller_process();
  if (!process) return process.error();
  return release_capture(**process);
}
