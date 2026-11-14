#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/utf8.h"
#include "private/win_internal.h"

// Modifier mask of the last native key message copied; note_key applies it.
static uint32_t lastKeyModifiers;
static BOOL lastKeyModifiersValid;

void copy_message(MSG *destination, const UiMessage *source) {
  lastKeyModifiersValid = source->message >= WM_KEYDOWN && source->message <= WM_SYSCHAR;
  lastKeyModifiers = source->modifiers;
  destination->hwnd = hwnd_from_value(source->hwnd);
  destination->message = source->message;
  destination->wParam = source->wParam;
  destination->lParam = (LPARAM)(int32_t)source->lParam;
  destination->time = source->time;
  /* Win32 MSG.pt is the cursor position in screen coordinates. Mouse
     messages carry it in lParam (client coordinates); others reuse the last. */
  static POINT lastPoint;
  if (source->message >= WM_MOUSEMOVE && source->message <= 0x020E &&
      destination->hwnd) {
    POINT point = {(SHORT)LOWORD(destination->lParam), (SHORT)HIWORD(destination->lParam)};
    if (ClientToScreen(destination->hwnd, &point)) lastPoint = point;
  }
  destination->pt = lastPoint;
}

void note_key(const MSG *message) {
  if (message->message != WM_KEYDOWN && message->message != WM_KEYUP &&
      message->message != WM_SYSKEYDOWN && message->message != WM_SYSKEYUP)
    return;
  const UINT key = (UINT)(message->wParam & 0xFFu);
  const BOOL down = message->message == WM_KEYDOWN ||
                    message->message == WM_SYSKEYDOWN;
  if (lastKeyModifiersValid) {
    // The kernel's mask is authoritative for the modifier keys: Shift 1|2,
    // Control 4|8, Alt 16|32, Caps 64, Num 128, Scroll 256.
    lastKeyModifiersValid = FALSE;
    static const struct { UINT key; uint32_t mask; BOOL toggle; } map[] = {
        {VK_LSHIFT, 1, FALSE},     {VK_RSHIFT, 2, FALSE},
        {VK_SHIFT, 3, FALSE},      {VK_LCONTROL, 4, FALSE},
        {VK_RCONTROL, 8, FALSE},   {VK_CONTROL, 12, FALSE},
        {VK_LMENU, 16, FALSE},     {VK_RMENU, 32, FALSE},
        {VK_MENU, 48, FALSE},      {VK_CAPITAL, 64, TRUE},
        {VK_NUMLOCK, 128, TRUE},   {VK_SCROLL, 256, TRUE}};
    for (UINT i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
      BYTE *state = &keyState[map[i].key];
      const BOOL on = (lastKeyModifiers & map[i].mask) != 0;
      if (map[i].toggle) *state = (BYTE)((*state & 0x80u) | (on ? 1u : 0u));
      else if (on) *state |= 0x80u;
      else *state &= (BYTE)~0x80u;
    }
  }
  UINT keys[2] = {key, 0};
  if (key == VK_LSHIFT || key == VK_RSHIFT) keys[1] = VK_SHIFT;
  else if (key == VK_LCONTROL || key == VK_RCONTROL) keys[1] = VK_CONTROL;
  else if (key == VK_LMENU || key == VK_RMENU) keys[1] = VK_MENU;
  for (int i = 0; i < 2; ++i) {
    if (i && !keys[i]) break;
    BYTE *state = &keyState[keys[i]];
    if (down) {
      if (!(*state & 0x80u)) *state ^= 0x01u;
      *state |= 0x80u;
    } else {
      *state &= (BYTE)~0x80u;
    }
  }
}

SHORT GetKeyState(int virtualKey) {
  if (virtualKey < 0 || virtualKey > 255) return 0;
  const BYTE state = keyState[virtualKey];
  return (SHORT)(((state & 0x80u) ? 0x8000u : 0u) | (state & 0x01u));
}

SHORT GetAsyncKeyState(int virtualKey) {
  if (virtualKey < 0 || virtualKey > 255) return 0;
  return (SHORT)((keyState[virtualKey] & 0x80u) ? 0x8000u : 0u);
}

void quit_message(MSG *message) {
  message->hwnd = NULL;
  message->message = WM_QUIT;
  message->wParam = (WPARAM)quitCode;
  message->lParam = 0;
  message->time = 0;
  message->pt.x = 0;
  message->pt.y = 0;
}

int GetMessage(MSG *message, HWND window, UINT minimum, UINT maximum) {
  if (!message) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return -1;
  }
  if (window || minimum || maximum) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return -1;
  }
  UiMessage native = {0};
  if (quitPending) {
    uint32_t status = ui_backend_peek_message(&native, 1);
    if (status == UI_STATUS_TIMEOUT) {
      quit_message(message);
      quitPending = FALSE;
      return 0;
    }
    if (!backend_ok(status)) return -1;
  } else if (ui_timer_active()) {
    for (;;) {
      const uint32_t status = ui_backend_peek_message(&native, 1);
      if (status == UI_STATUS_OK) break;
      if (status != UI_STATUS_TIMEOUT) {
        (void)backend_ok(status);
        return -1;
      }
      if (quitPending) {
        quit_message(message);
        quitPending = FALSE;
        return 0;
      }
      if (ui_timer_message(message, TRUE)) return 1;
      if (!ui_timer_active()) {
        if (!backend_ok(ui_backend_get_message(&native))) return -1;
        break;
      }
      // Sleep until a message arrives or the next timer is due.
      const uint32_t wait = ui_timer_wait_ms();
      const uint32_t waited = ui_backend_wait_message(wait);
      if (waited != UI_STATUS_OK && waited != UI_STATUS_TIMEOUT) {
        (void)backend_ok(waited);
        return -1;
      }
    }
  } else if (!backend_ok(ui_backend_get_message(&native))) {
    return -1;
  }
  copy_message(message, &native);
  if (message->message == WM_QUIT) {
    quitPending = FALSE;
    return 0;
  }
  note_key(message);
  return 1;
}

BOOL WaitMessage(void) {
  if (quitPending) return TRUE;
  uint32_t timeout = UINT32_MAX;
  if (ui_timer_active()) {
    timeout = ui_timer_wait_ms();
    if (!timeout) return TRUE;
  }
  const uint32_t status = ui_backend_wait_message(timeout);
  if (status == UI_STATUS_TIMEOUT) return TRUE;
  return backend_ok(status);
}

BOOL PeekMessage(MSG *message, HWND window, UINT minimum, UINT maximum,
                 UINT remove) {
  if (!message) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (window || minimum || maximum) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
  }
  if (remove != PM_NOREMOVE && remove != PM_REMOVE) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
  }
  UiMessage native = {0};
  uint32_t status = ui_backend_peek_message(&native, remove == PM_REMOVE);
  if (status == UI_STATUS_TIMEOUT) {
    if (!quitPending) return ui_timer_message(message, remove == PM_REMOVE);
    quit_message(message);
    if (remove == PM_REMOVE) quitPending = FALSE;
    return TRUE;
  }
  if (!backend_ok(status)) return FALSE;
  copy_message(message, &native);
  if (remove == PM_REMOVE && message->message == WM_QUIT) quitPending = FALSE;
  if (remove == PM_REMOVE) note_key(message);
  return TRUE;
}

BOOL PostMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (wParam > UINT32_MAX || lParam < INT32_MIN ||
      (lParam > 0 && (uint64_t)lParam > UINT32_MAX)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const uint32_t handle = hwnd_value(window);
  const WindowRecord *record = live_window_by_handle(handle);
  if (record && record->recoveryOnly) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  return backend_ok(ui_backend_post_message(handle, message,
      (uint32_t)wParam, (uint32_t)lParam));
}

void PostQuitMessage(int exitCode) {
  quitCode = exitCode;
  quitPending = TRUE;
}

LRESULT call_window_procedure(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  if (!record || !record->alive) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  if (message == WM_DISPLAYCHANGE && record->uiContext &&
      record->uiContext->hasDisplayDc)
    record->uiContext->displayDcStale = TRUE;
  uint32_t ignored;
  if (!backend_ok(ui_backend_get_window_long(record->handle,
                                              UI_WINDOW_USERDATA, &ignored)))
    return 0;
  WNDPROC procedure = record->procedure;
  if (!procedure) return 0;
  ++record->active;
  LRESULT result = procedure(window, message, wParam, lParam);
  --record->active;
  if (!record->active && !record->alive) {
    uint32_t handle = record->handle;
    record->used = FALSE;
    forget_window_dcs(handle);
  }
  return result;
}

LRESULT DispatchMessage(const MSG *message) {
  if (!message || message->message == WM_QUIT) return 0;
  LRESULT timerResult;
  if (ui_timer_dispatch(message, &timerResult)) return timerResult;
  if (!message->hwnd) return 0;
  if (message->message == WM_CREATE && !message->lParam) {
    const WindowRecord *record = window_by_handle(hwnd_value(message->hwnd));
    if (record && record->createSent) return 0;
  }
  return call_window_procedure(message->hwnd, message->message,
                               message->wParam, message->lParam);
}

LRESULT SendMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (!window) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  return call_window_procedure(window, message, wParam, lParam);
}

LRESULT DefWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  if (message == WM_DISPLAYCHANGE && record->uiContext &&
      record->uiContext->hasDisplayDc)
    record->uiContext->displayDcStale = TRUE;
  if (message == WM_NCLBUTTONDOWN && wParam == HTSYSMENU &&
      record->uiContext) {
    ui_default_system_menu(window, lParam);
    return 0;
  }
  if (message == WM_SYSCOMMAND) {
    const UINT command = (UINT)wParam & 0xFFF0u;
    if (command == SC_CLOSE) {
      MSG close = {0};
      close.hwnd = window;
      close.message = WM_CLOSE;
      return DispatchMessage(&close);
    }
    if (command == SC_RESTORE) return UndosShowWindow(window, SW_RESTORE);
    if (command == SC_MINIMIZE) return UndosShowWindow(window, SW_MINIMIZE);
    if (command == SC_MAXIMIZE) return UndosShowWindow(window, SW_MAXIMIZE);
    if (command == SC_MOVE || command == SC_SIZE) {
      if (record->uiContext) {
        ui_default_system_geometry(window, command);
        return 0;
      }
    }
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
  }
  if (message == WM_PAINT) {
    // Validate the update region, as Win32 does; otherwise the kernel keeps
    // reposting WM_PAINT and the process spins.
    PAINTSTRUCT paint;
    if (BeginPaint(window, &paint)) EndPaint(window, &paint);
    return 0;
  }
  if (message == WM_CLOSE) (void)DestroyWindow(window);
  return 0;
}

BOOL TranslateMessage(const MSG *message) {
  (void)message;
  return FALSE;
}
