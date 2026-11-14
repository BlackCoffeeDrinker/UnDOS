#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/win_internal.h"

/* Timers, mouse capture, cursors and drag detection. Timers are generated
   by the message loop when the queue is empty, as Win32 does: WM_TIMER is
   the lowest-priority message and repeated expiries coalesce into one. */

#define UI_TIMER_LIMIT 32
#define UI_DRAG_IDLE_LIMIT 500

typedef struct TimerRecord {
  BOOL used;
  uint32_t window;
  UINT_PTR id;
  UINT interval;
  uint32_t due;
  TIMERPROC procedure;
} TimerRecord;

static TimerRecord timers[UI_TIMER_LIMIT];
static UINT_PTR nextThreadTimer = 0x7000;
static uint32_t captureWindow;
static HCURSOR currentCursor;
static int cursorCount;

void ui_input_reset(void) {
  memset(timers, 0, sizeof(timers));
  nextThreadTimer = 0x7000;
  captureWindow = 0;
  currentCursor = NULL;
  cursorCount = 0;
}

void ui_input_forget(uint32_t handle) {
  for (size_t i = 0; i < UI_TIMER_LIMIT; ++i)
    if (timers[i].used && timers[i].window == handle) timers[i].used = FALSE;
  if (captureWindow == handle) {
    captureWindow = 0;
    (void)ui_backend_release_capture();
  }
}

DWORD GetTickCount(void) { return ui_backend_tick_ms(); }

UINT GetDoubleClickTime(void) { return 500; }

static TimerRecord *find_timer(uint32_t window, UINT_PTR id) {
  for (size_t i = 0; i < UI_TIMER_LIMIT; ++i)
    if (timers[i].used && timers[i].window == window && timers[i].id == id)
      return &timers[i];
  return NULL;
}

UINT_PTR SetTimer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC procedure) {
  const uint32_t handle = hwnd_value(window);
  if (handle && !live_window_by_handle(handle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  if (elapse < USER_TIMER_MINIMUM) elapse = USER_TIMER_MINIMUM;
  if (elapse > USER_TIMER_MAXIMUM) elapse = USER_TIMER_MAXIMUM;
  TimerRecord *timer = handle ? find_timer(handle, id) : NULL;
  if (!handle) {
    timer = id ? find_timer(0, id) : NULL;
    if (!timer) id = nextThreadTimer++;
  }
  for (size_t i = 0; i < UI_TIMER_LIMIT && !timer; ++i)
    if (!timers[i].used) timer = &timers[i];
  if (!timer) {
    local_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  timer->used = TRUE;
  timer->window = handle;
  timer->id = id;
  timer->interval = elapse;
  timer->due = ui_backend_tick_ms() + elapse;
  timer->procedure = procedure;
  return handle && !id ? 1 : id;
}

BOOL KillTimer(HWND window, UINT_PTR id) {
  TimerRecord *timer = find_timer(hwnd_value(window), id);
  if (!timer) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  timer->used = FALSE;
  return TRUE;
}

BOOL ui_timer_active(void) {
  for (size_t i = 0; i < UI_TIMER_LIMIT; ++i)
    if (timers[i].used) return TRUE;
  return FALSE;
}

uint32_t ui_timer_wait_ms(void) {
  const uint32_t now = ui_backend_tick_ms();
  uint32_t best = UINT32_MAX;
  for (size_t i = 0; i < UI_TIMER_LIMIT; ++i) {
    if (!timers[i].used) continue;
    const int32_t left = (int32_t)(timers[i].due - now);
    const uint32_t wait = left > 0 ? (uint32_t)left : 0;
    if (wait < best) best = wait;
  }
  return best;
}

BOOL ui_timer_message(MSG *message, BOOL remove) {
  const uint32_t now = ui_backend_tick_ms();
  TimerRecord *due = NULL;
  for (size_t i = 0; i < UI_TIMER_LIMIT; ++i) {
    TimerRecord *timer = &timers[i];
    if (!timer->used || (int32_t)(now - timer->due) < 0) continue;
    if (!due || (int32_t)(timer->due - due->due) < 0) due = timer;
  }
  if (!due) return FALSE;
  memset(message, 0, sizeof(*message));
  message->hwnd = hwnd_from_value(due->window);
  message->message = WM_TIMER;
  message->wParam = due->id;
  message->lParam = (LPARAM)(uintptr_t)due->procedure;
  message->time = now;
  if (remove) due->due = now + due->interval;
  return TRUE;
}

BOOL ui_timer_dispatch(const MSG *message, LRESULT *result) {
  if (message->message != WM_TIMER || !message->lParam) return FALSE;
  TIMERPROC procedure = (TIMERPROC)(uintptr_t)message->lParam;
  // Only call back procedures that belong to a live timer.
  BOOL known = FALSE;
  for (size_t i = 0; i < UI_TIMER_LIMIT && !known; ++i)
    known = timers[i].used && timers[i].procedure == procedure &&
            timers[i].id == message->wParam;
  if (!known) return FALSE;
  procedure(message->hwnd, WM_TIMER, message->wParam, GetTickCount());
  *result = 0;
  return TRUE;
}

HWND GetCapture(void) {
  if (captureWindow && !live_window_by_handle(captureWindow)) captureWindow = 0;
  return hwnd_from_value(captureWindow);
}

HWND SetCapture(HWND window) {
  const uint32_t handle = hwnd_value(window);
  const HWND previous = GetCapture();
  if (!handle || !live_window_by_handle(handle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return previous;
  }
  if (!backend_ok(ui_backend_set_capture(handle))) return previous;
  // The compositor posts WM_CAPTURECHANGED to the window losing capture.
  captureWindow = handle;
  return previous;
}

BOOL ReleaseCapture(void) {
  captureWindow = 0;
  return backend_ok(ui_backend_release_capture());
}

static BOOL stock_cursor(uintptr_t id) {
  return (id >= 32512 && id <= 32516) || (id >= 32640 && id <= 32650 &&
                                          id != 32647);
}

HCURSOR LoadCursor(HINSTANCE instance, LPCSTR name) {
  const uintptr_t id = (uintptr_t)name;
  if (!instance && IS_INTRESOURCE(name) && stock_cursor(id))
    return (HCURSOR)id;
  HRSRC resource = FindResource(instance, name, RT_CURSOR);
  return (HCURSOR)resource;
}

/* NOT_YET_VERIFIED: the compositor has no cursor-shape operation, so the
   pointer on screen does not change; the selection is only recorded. */
HCURSOR SetCursor(HCURSOR cursor) {
  const HCURSOR previous = currentCursor;
  currentCursor = cursor;
  return previous;
}

HCURSOR GetCursor(void) { return currentCursor; }

int ShowCursor(BOOL show) {
  cursorCount += show ? 1 : -1;
  return cursorCount;
}

int GetSystemMetrics(int index) {
  switch (index) {
    case SM_CXVSCROLL:
    case SM_CYHSCROLL: return 16;
    case SM_CYCAPTION: return 20;
    case SM_CXBORDER:
    case SM_CYBORDER: return 1;
    case SM_CXICON:
    case SM_CYICON: return 32;
    case SM_CYMENU: return 20;
    case SM_CXDOUBLECLK:
    case SM_CYDOUBLECLK:
    case SM_CXDRAG:
    case SM_CYDRAG: return 4;
    default: return 0;
  }
}

static BOOL input_message(UINT message) {
  return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
         (message >= WM_KEYDOWN && message <= WM_SYSCHAR);
}

/* point is in the client coordinates of window (UnDOS reports mouse
   positions client-relative and has no ScreenToClient yet). */
BOOL DragDetect(HWND window, POINT point) {
  if (!live_window_by_handle(hwnd_value(window))) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  const int dx = GetSystemMetrics(SM_CXDRAG), dy = GetSystemMetrics(SM_CYDRAG);
  BOOL dragged = FALSE;
  int idle = 0;
  (void)SetCapture(window);
  for (;;) {
    MSG message;
    if (!PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
      if (++idle > UI_DRAG_IDLE_LIMIT) break;
      ui_backend_sleep(10);
      continue;
    }
    idle = 0;
    if (message.message == WM_QUIT) {
      PostQuitMessage((int)message.wParam);
      break;
    }
    if (!input_message(message.message)) {
      (void)DispatchMessage(&message);
      continue;
    }
    if (message.message == WM_MOUSEMOVE && message.hwnd == window) {
      const int x = GET_X_LPARAM(message.lParam);
      const int y = GET_Y_LPARAM(message.lParam);
      if (x < point.x - dx || x > point.x + dx || y < point.y - dy ||
          y > point.y + dy) {
        dragged = TRUE;
        break;
      }
    }
    if (message.message == WM_LBUTTONUP || message.message == WM_RBUTTONUP ||
        (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE))
      break;
  }
  (void)ReleaseCapture();
  return dragged;
}
