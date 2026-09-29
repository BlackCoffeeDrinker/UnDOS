#include "stdkrn.hpp"
#include "syscalls.hpp"

#include "user/user.hpp"

// USER (window manager) syscall handlers (400-499), generated from
// tools/usersyscall.txt. Window procedures run in userland: the kernel only
// stores the WNDPROC value and hands it back through GetWindowLongPtr.

namespace {
using namespace kernel::user;

kernel::KProcessObject *current_process() {
  const auto thread = KE_SCHED_GetCurrentThread();
  return thread ? thread->owner_process : nullptr;
}

URect to_urect(const Rect &r) { return {r.left, r.top, r.right, r.bottom}; }

void copy_string(char *dst, const kstd::string &src, uint32_t len) {
  uint32_t i = 0;
  for (; i + 1 < len && i < src.size(); ++i) dst[i] = src[i];
  dst[i] = 0;
}

// Looks up a window owned by the calling process.
KWindowObject *own_window_locked(uint32_t hwnd) {
  auto *w = lookup_locked(hwnd);
  return w && w->process == current_process() ? w : nullptr;
}

void to_msg(const QueuedMessage &m, Msg &out) {
  out.hwnd = m.hwnd;
  out.message = m.message;
  out.wParam = m.wParam;
  out.lParam = m.lParam;
  out.time = m.time;
}
}// namespace

SyscallReturnCode handle_RegisterClass_V1(__KRN_USER_REGISTERCLASS_V1 &&args) {
  const auto proc = current_process();
  if (!proc || args.name.size() == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  char name[kNameLen];
  copy_string(name, args.name, kNameLen);
  Guard guard;
  return register_class_locked(*proc, name, args.wndProc, args.background) ? SyscallReturnCode_OKAY
                                                                           : SyscallReturnCode_OUT_OF_MEMORY;
}

SyscallReturnCode handle_CreateWindow_V1(__KRN_USER_CREATEWINDOW_V1 &&args) {
  const auto proc = current_process();
  if (!proc || args.width <= 0 || args.height <= 0) return SyscallReturnCode_INVALID_ARGUMENT;

  char className[kNameLen];
  copy_string(className, args.className, kNameLen);

  // Allocate outside the lock; only the table insert happens under it.
  auto window = kernel::CreateKObject<KWindowObject>("window");
  if (!window) return SyscallReturnCode_OUT_OF_MEMORY;
  copy_string(window->title, args.title, kTitleLen);
  window->style = static_cast<uint32_t>(args.style);
  window->rect = {args.x, args.y, args.x + args.width, args.y + args.height};
  window->parent = args.parent;
  window->userData = args.userData;
  window->process = proc;

  Guard guard;
  const int32_t cls = find_class_locked(className);
  if (cls < 0) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto *wc = class_at_locked(static_cast<uint32_t>(cls));
  window->classIndex = static_cast<uint32_t>(cls);
  window->wndProc = wc->wndProc;
  window->background = wc->background;
  window->queue = queue_for_current_locked(true);
  if (!window->queue) return SyscallReturnCode_OUT_OF_MEMORY;

  const uint32_t hwnd = insert_window_locked(window);
  if (!hwnd) return SyscallReturnCode_OUT_OF_MEMORY;
  post_locked(*window->queue, hwnd, kMsgCreate, 0, 0);
  args.hwnd = hwnd;
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_DestroyWindow_V1(__KRN_USER_DESTROYWINDOW_V1 &&args) {
  Guard guard;
  auto *w = own_window_locked(args.hwnd);
  if (!w) return SyscallReturnCode_INVALID_ARGUMENT;
  destroy_window_locked(*w);
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_ShowWindow_V1(__KRN_USER_SHOWWINDOW_V1 &&args) {
  Guard guard;
  auto *w = own_window_locked(args.hwnd);
  if (!w) return SyscallReturnCode_INVALID_ARGUMENT;
  show_window_locked(*w, args.command != ShowCommand_Hide);
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_MoveWindow_V1(__KRN_USER_MOVEWINDOW_V1 &&args) {
  if (args.width <= 0 || args.height <= 0) return SyscallReturnCode_INVALID_ARGUMENT;
  Guard guard;
  auto *w = own_window_locked(args.hwnd);
  if (!w) return SyscallReturnCode_INVALID_ARGUMENT;
  move_window_locked(*w, {args.x, args.y, args.x + args.width, args.y + args.height});
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_GetWindowRect_V1(__KRN_USER_GETWINDOWRECT_V1 &&args) {
  Guard guard;
  auto *w = lookup_locked(args.hwnd);
  if (!w) return SyscallReturnCode_INVALID_ARGUMENT;
  args.rect = Rect{w->rect.left, w->rect.top, w->rect.right, w->rect.bottom};
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_InvalidateRect_V1(__KRN_USER_INVALIDATERECT_V1 &&args) {
  Guard guard;
  auto *w = own_window_locked(args.hwnd);
  if (!w) return SyscallReturnCode_INVALID_ARGUMENT;
  const URect whole{0, 0, w->rect.right - w->rect.left, w->rect.bottom - w->rect.top};
  invalidate_locked(*w, args.whole ? whole : to_urect(args.rect));
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_BeginPaint_V1(__KRN_USER_BEGINPAINT_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  Guard guard;
  auto *w = own_window_locked(args.hwnd);
  if (!w) return SyscallReturnCode_INVALID_ARGUMENT;
  URect paint{};
  uint32_t hdc = 0;
  if (!begin_paint_locked(*proc, *w, hdc, paint)) return SyscallReturnCode_OUT_OF_MEMORY;
  args.dc = hdc;
  args.paint.rcPaint = Rect{paint.left, paint.top, paint.right, paint.bottom};
  args.paint.erased = 1;
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_EndPaint_V1(__KRN_USER_ENDPAINT_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  // The area was validated by BeginPaint; only the DC is released here.
  return proc->dcTable.Close(args.dc) ? SyscallReturnCode_OKAY : SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_GetMessage_V1(__KRN_USER_GETMESSAGE_V1 &&args) {
  QueuedMessage m;
  if (!get_message(m, true, true)) return SyscallReturnCode_OUT_OF_MEMORY;
  to_msg(m, args.msg);
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_PeekMessage_V1(__KRN_USER_PEEKMESSAGE_V1 &&args) {
  QueuedMessage m;
  if (!get_message(m, false, args.remove != 0)) return SyscallReturnCode_TIMEOUT;
  to_msg(m, args.msg);
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_PostMessage_V1(__KRN_USER_POSTMESSAGE_V1 &&args) {
  Guard guard;
  MessageQueue *q = nullptr;
  if (args.hwnd == 0) {
    q = queue_for_current_locked(true);
  } else if (auto *w = lookup_locked(args.hwnd)) {
    q = w->queue;
  } else {
    return SyscallReturnCode_INVALID_ARGUMENT;
  }
  if (!q) return SyscallReturnCode_INVALID_ARGUMENT;
  return post_locked(*q, args.hwnd, args.message, args.wParam, args.lParam) ? SyscallReturnCode_OKAY
                                                                            : SyscallReturnCode_OUT_OF_MEMORY;
}

SyscallReturnCode handle_GetWindowLongPtr_V1(__KRN_USER_GETWINDOWLONGPTR_V1 &&args) {
  Guard guard;
  auto *w = lookup_locked(args.hwnd);
  if (!w) return SyscallReturnCode_INVALID_ARGUMENT;
  switch (args.index) {
    case WindowLongIndex_WndProc: args.value = w->wndProc; break;
    case WindowLongIndex_UserData: args.value = w->userData; break;
    case WindowLongIndex_Style: args.value = w->style; break;
    default: return SyscallReturnCode_INVALID_ARGUMENT;
  }
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_SetWindowLongPtr_V1(__KRN_USER_SETWINDOWLONGPTR_V1 &&args) {
  Guard guard;
  auto *w = own_window_locked(args.hwnd);
  if (!w) return SyscallReturnCode_INVALID_ARGUMENT;
  switch (args.index) {
    case WindowLongIndex_WndProc: args.previous = w->wndProc; w->wndProc = args.value; break;
    case WindowLongIndex_UserData: args.previous = w->userData; w->userData = args.value; break;
    case WindowLongIndex_Style: {
      args.previous = w->style;
      const bool show = (args.value & kStyleVisible) != 0;
      w->style = (args.value & ~kStyleVisible) | (w->style & kStyleVisible);
      show_window_locked(*w, show);
      break;
    }
    default: return SyscallReturnCode_INVALID_ARGUMENT;
  }
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_SetCapture_V1(__KRN_USER_SETCAPTURE_V1 &&args) {
  Guard guard;
  if (!own_window_locked(args.hwnd)) return SyscallReturnCode_INVALID_ARGUMENT;
  set_capture_locked(args.hwnd);
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_ReleaseCapture_V1(__KRN_USER_RELEASECAPTURE_V1 &&args) {
  (void) args;
  Guard guard;
  if (own_window_locked(capture_locked())) set_capture_locked(0);
  return SyscallReturnCode_OKAY;
}
