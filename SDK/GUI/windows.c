#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/utf8.h"
#include "private/win_internal.h"

ATOM RegisterClass(const WNDCLASS *windowClass) {
  if (!windowClass) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (!windowClass->lpfnWndProc) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (!utf8_string(windowClass->lpszClassName, UI_CLASS_NAME_BYTES, NULL, FALSE))
    return 0;
  /* Class styles, icons and cursors are recorded but have no effect: the
     kernel always sends double-clicks and repaints on resize. cbClsExtra
     is accepted and not stored (GetClassLong is not provided). */
  if (windowClass->cbWndExtra < 0 || windowClass->cbWndExtra > UI_WND_EXTRA_BYTES ||
      windowClass->cbClsExtra < 0) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  const char *menuName = windowClass->lpszMenuName;
  const BOOL menuIsId = menuName && IS_INTRESOURCE(menuName);
  if (menuName && !menuIsId && strlen(menuName) > UI_MENU_NAME_BYTES) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (class_by_name(windowClass->lpszClassName)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_CLASS_ALREADY_EXISTS);
    return 0;
  }
  uintptr_t brush = (uintptr_t)windowClass->hbrBackground;
  uint32_t background = 7;
  if (brush) {
    if (brush > 16) {
      local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      return 0;
    }
    background = (uint32_t)(brush - 1);
  }
  ClassRecord *record = class_slot();
  if (!record) {
    local_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  uint32_t nativeAtom = 0;
  uint32_t status = ui_backend_register_class(windowClass->lpszClassName,
      (uintptr_t)windowClass->lpfnWndProc, background, &nativeAtom);
  if (!backend_ok(status)) return 0;
  if (!nextAtom) nextAtom = 1;
  ATOM atom = nextAtom++;
  record->used = TRUE;
  record->atom = atom;
  record->procedure = windowClass->lpfnWndProc;
  record->background = background;
  record->style = windowClass->style;
  record->wndExtra = windowClass->cbWndExtra;
  record->icon = windowClass->hIcon;
  record->cursor = windowClass->hCursor;
  record->instance = windowClass->hInstance;
  record->menuId = menuIsId ? (uintptr_t)menuName : 0;
  record->menuName[0] = 0;
  if (menuName && !menuIsId) strcpy(record->menuName, menuName);
  size_t i = 0;
  while (windowClass->lpszClassName[i]) {
    record->name[i] = windowClass->lpszClassName[i];
    ++i;
  }
  record->name[i] = 0;
  return atom;
}

HWND CreateWindowEx(DWORD exStyle, LPCSTR className, LPCSTR windowName,
                    DWORD style, int x, int y, int width, int height,
                    HWND parent, HMENU menu, HINSTANCE instance,
                    LPVOID parameter) {
  (void)instance;
  if (exStyle) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
  }
  if (!utf8_string(className, UI_CLASS_NAME_BYTES, NULL, FALSE) ||
      (windowName && !utf8_string(windowName, UI_TITLE_BYTES, NULL, TRUE)))
    return NULL;
  if (!windowName) windowName = "";
  className = ui_controls_class_name(className);
  if (!valid_window_style(style)) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
  }
  ClassRecord *klass = class_by_name(className);
  if (!klass) {
    local_failure(UI_STATUS_NOT_FOUND, ERROR_CLASS_DOES_NOT_EXIST);
    return NULL;
  }
  const uint32_t parentValue = hwnd_value(parent);
  if (((style & WS_CHILD) && !parentValue) ||
      (!(style & WS_CHILD) && parentValue) ||
      (parentValue && !window_by_handle(parentValue))) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  // For a child window the menu argument is its control ID.
  const UINT id = (style & WS_CHILD) ? (UINT)(uintptr_t)menu : 0;
  if (style & WS_CHILD) menu = NULL;
  // As in Win32, a default x makes y ignored, and a default width makes
  // height ignored (both get the default size).
  if (x == CW_USEDEFAULT) x = y = 0;
  if (y == CW_USEDEFAULT) y = 0;
  if (width == CW_USEDEFAULT) {
    width = 320;
    height = CW_USEDEFAULT;
  }
  if (height == CW_USEDEFAULT) {
    UNDOS_FONT font;
    uint32_t generation;
    UNDOS_TEXT_METRICS metrics;
    BOOL acquired = UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT,
        UNDOS_GLYPH_FALLBACK_NOTDEF, &font, &generation);
    if (!acquired) return NULL;
    if (!UndosGetFontMetrics(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_BASIC,
                             &metrics)) {
      DWORD error = GetLastError();
      DWORD status = UndosGetLastStatus();
      if (!UndosCloseFont(font)) return NULL;
      local_failure(status, error);
      return NULL;
    }
    (void)generation;
    if (!UndosCloseFont(font)) return NULL;
    uint64_t value = 10ull * metrics.lineHeight + 24;
    if (!metrics.lineHeight || value > INT32_MAX) {
      local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      return NULL;
    }
    height = (int)value;
  }
  /* Win32 accepts empty (and negative) sizes; the window is resized later.
     The kernel needs at least one pixel. */
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  WindowRecord *record = window_slot();
  if (!record) {
    local_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  uint32_t handle = 0;
  uint32_t status = ui_backend_create_window(className, windowName,
      style_to_native(style), x, y, width, height, parentValue,
      (uint32_t)(uintptr_t)parameter, &handle);
  if (!backend_ok(status)) return NULL;
  if (!handle) {
    (void)ui_backend_destroy_window(handle);
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  record->used = TRUE;
  record->alive = TRUE;
  record->destroying = FALSE;
  record->recoveryOnly = FALSE;
  record->createSent = FALSE;
  record->uiRecovery = FALSE;
  record->active = 0;
  if (!nextWindowIdentity) nextWindowIdentity = 1;
  record->identity = nextWindowIdentity++;
  record->handle = handle;
  record->parent = parentValue;
  record->uiContext = NULL;
  record->setupError = record->setupStatus = 0;
  record->cleanupError = record->cleanupStatus = 0;
  record->userData = (LONG_PTR)(uint32_t)(uintptr_t)parameter;
  record->style = style;
  record->procedure = klass->procedure;
  record->id = id;
  record->disabled = (style & WS_DISABLED) != 0;
  record->extraBytes = klass->wndExtra;
  memset(record->extra, 0, sizeof(record->extra));
  ui_copy_text(record->title, sizeof(record->title), windowName);
  HWND created = hwnd_from_value(handle);
  ui_controls_attach(created, windowName);
  ui_mdi_attach(created, parameter);
  if ((style & WS_VISIBLE) && !UndosShowWindow(created, SW_SHOW)) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    const uint32_t cleanupStatus = ui_backend_destroy_window(handle);
    if (cleanupStatus == UI_STATUS_OK) {
      forget_descendants(handle);
      forget_window(record);
    } else {
      record->recoveryOnly = TRUE;
      record->setupError = error;
      record->setupStatus = raw;
      record->cleanupError = error_for_status(cleanupStatus);
      record->cleanupStatus = cleanupStatus;
    }
    local_failure(raw, error);
    return NULL;
  }
  if (!menu && !(style & WS_CHILD) && (klass->menuId || klass->menuName[0])) {
    /* As in Win32, a top-level window without a menu gets its class menu. */
    menu = LoadMenu(klass->instance, klass->menuId ? (LPCSTR)klass->menuId
                                                   : klass->menuName);
  }
  if (menu && !SetMenu(created, menu)) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    const uint32_t cleanupStatus = ui_backend_destroy_window(handle);
    if (cleanupStatus == UI_STATUS_OK) {
      forget_descendants(handle);
      forget_window(record);
    } else {
      record->recoveryOnly = TRUE;
      record->setupError = error;
      record->setupStatus = raw;
      record->cleanupError = error_for_status(cleanupStatus);
      record->cleanupStatus = cleanupStatus;
    }
    local_failure(raw, error);
    return NULL;
  }
  /* As in Win32, WM_CREATE is sent before CreateWindowEx returns, with a
     CREATESTRUCT, and -1 fails the creation. The kernel's queued WM_CREATE
     for this window is then skipped by DispatchMessage. */
  CREATESTRUCT cs = {parameter, instance, (style & WS_CHILD) ? (HMENU)(uintptr_t)id : menu,
                     hwnd_from_value(parentValue), height, width, y, x,
                     (LONG)style, windowName, className, exStyle};
  record->createSent = TRUE;
  if (call_window_procedure(created, WM_CREATE, 0, (LPARAM)(uintptr_t)&cs) == -1) {
    (void)DestroyWindow(created);
    if (GetLastError() == 0) SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return created;
}

HWND CreateWindow(LPCSTR className, LPCSTR windowName, DWORD style,
                  int x, int y, int width, int height, HWND parent, HMENU menu,
                  HINSTANCE instance, LPVOID parameter) {
  return CreateWindowEx(0, className, windowName, style, x, y, width, height,
                        parent, menu, instance, parameter);
}

BOOL DestroyWindow(HWND window) {
  uint32_t handle = hwnd_value(window);
  WindowRecord *record = live_window_by_handle(handle);
  if (!record || !record->alive) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if (record->destroying) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if (record->recoveryOnly) {
    record->destroying = TRUE;
    if (record->uiRecovery) {
      if (!cleanup_window_dcs(handle)) {
        record->cleanupStatus = lastStatus;
        record->cleanupError = lastError;
        record->destroying = FALSE;
        return FALSE;
      }
      if (record->hasCleanupFont) {
        const uint32_t cleanup = ui_backend_close_font(record->cleanupFont);
        if (cleanup != UI_STATUS_OK) {
          record->cleanupStatus = cleanup;
          record->cleanupError = error_for_status(cleanup);
          record->destroying = FALSE;
          failure(cleanup);
          return FALSE;
        }
        record->hasCleanupFont = FALSE;
      }
    }
    const uint32_t status = ui_backend_destroy_window(handle);
    record->destroying = FALSE;
    if (status != UI_STATUS_OK) {
      record->cleanupStatus = status;
      record->cleanupError = error_for_status(status);
      failure(status);
      return FALSE;
    }
    record->alive = FALSE;
    forget_descendants(handle);
    forget_window(record);
    return TRUE;
  }
  const uint64_t identity = record->identity;
  record->destroying = TRUE;
  MSG notification = {0};
  notification.hwnd = window;
  notification.message = WM_DESTROY;
  (void)DispatchMessage(&notification);
  record = live_window_by_handle(handle);
  if (!record || record->identity != identity) return TRUE;
  uint32_t status = ui_backend_destroy_window(handle);
  if (!backend_ok(status)) {
    record->destroying = FALSE;
    return FALSE;
  }
  record->alive = FALSE;
  record->destroying = FALSE;
  forget_descendants(handle);
  forget_window(record);
  return TRUE;
}

BOOL UndosShowWindow(HWND window, int command) {
  uint32_t handle = hwnd_value(window);
  uint32_t native;
  if (!window_by_handle(handle) || !translate_show(command, &native)) {
    if (!window_by_handle(handle))
      local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  return backend_ok(ui_backend_show_window(handle, native));
}

BOOL UndosAttachUiContext(HWND window, UNDOS_UI_CONTEXT *context) {
  return ui_core_attach_context(window, context);
}

BOOL UndosSetFocus(HWND window) {
  const uint32_t handle = hwnd_value(window);
  if (handle && !window_by_handle(handle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  return backend_ok(ui_backend_set_focus(handle));
}

BOOL ShowWindow(HWND window, int command) {
  uint32_t handle = hwnd_value(window);
  uint32_t native;
  uint32_t oldStyle = 0;
  if (!window_by_handle(handle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if (!translate_show(command, &native)) return FALSE;
  if (!backend_ok(ui_backend_get_window_long(handle, UI_WINDOW_STYLE, &oldStyle)))
    return FALSE;
  BOOL wasVisible = (oldStyle & UI_WINDOW_VISIBLE) != 0;
  if (!backend_ok(ui_backend_show_window(handle, native))) return FALSE;
  return wasVisible;
}

BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint) {
  uint32_t handle = hwnd_value(window);
  if (!window_by_handle(handle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  RECT before, after;
  BOOL hadClient = GetClientRect(window, &before);
  if (!backend_ok(ui_backend_move_window(handle, x, y, width, height)))
    return FALSE;
  /* Win32 sends WM_SIZE before MoveWindow returns; callers (WINFILE's split
     bar) resize their own children from it. The kernel's later queued
     WM_SIZE repeats the same size and is harmless. */
  if (GetClientRect(window, &after) &&
      (!hadClient || after.right - after.left != before.right - before.left ||
       after.bottom - after.top != before.bottom - before.top))
    SendMessage(window, WM_SIZE, 0 /* SIZE_RESTORED */,
                MAKELPARAM(after.right - after.left, after.bottom - after.top));
  return !repaint || InvalidateRect(window, NULL, TRUE);
}

BOOL InvalidateRect(HWND window, const RECT *rect, BOOL erase) {
  uint32_t handle = hwnd_value(window);
  if (!window_by_handle(handle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if (erase != FALSE && erase != TRUE) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  UiRect native;
  const UiRect *nativeRect = NULL;
  if (rect) {
    native.left = rect->left;
    native.top = rect->top;
    native.right = rect->right;
    native.bottom = rect->bottom;
    nativeRect = &native;
  }
  return backend_ok(ui_backend_invalidate(handle, nativeRect, erase != FALSE));
}

static BOOL get_rect(HWND window, RECT *rect, uint32_t client) {
  UiRect result;
  if (!rect) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  uint32_t handle = hwnd_value(window);
  if (!window_by_handle(handle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if (!backend_ok(ui_backend_get_rect(handle, client, &result))) return FALSE;
  rect->left = result.left;
  rect->top = result.top;
  rect->right = result.right;
  rect->bottom = result.bottom;
  return TRUE;
}

BOOL GetClientRect(HWND window, RECT *rect) {
  return get_rect(window, rect, 1);
}

BOOL GetWindowRect(HWND window, RECT *rect) {
  return get_rect(window, rect, 0);
}

LONG_PTR GetWindowLongPtr(HWND window, int index) {
  uint32_t handle = hwnd_value(window);
  WindowRecord *record = window_by_handle(handle);
  if (!record || !record->alive) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  if (index == GWLP_WNDPROC) return (LONG_PTR)(uintptr_t)record->procedure;
  if (index == GWLP_USERDATA) return record->userData;
  if (index == GWL_ID) return (LONG_PTR)record->id;
  LONG_PTR dialogValue;
  if (index >= 0 && ui_dialog_window_long(handle, index, FALSE, 0, &dialogValue))
    return dialogValue;
  if (index >= 0 && index + (int)sizeof(LONG) <= record->extraBytes) {
    LONG value;
    memcpy(&value, record->extra + index, sizeof(value));
    return (LONG_PTR)value;
  }
  if (index == GWL_STYLE) {
    uint32_t native = 0;
    if (!backend_ok(ui_backend_get_window_long(handle, UI_WINDOW_STYLE, &native)))
      return 0;
    DWORD style = style_from_native(native);
    if (style & WS_CHILD) style |= record->style & WS_CHILD_EXTRA;
    if (record->disabled) style |= WS_DISABLED;
    else style &= ~WS_DISABLED;
    return (LONG_PTR)style;
  }
  local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
  return 0;
}

LONG_PTR SetWindowLongPtr(HWND window, int index, LONG_PTR value) {
  uint32_t handle = hwnd_value(window);
  WindowRecord *record = window_by_handle(handle);
  if (!record || !record->alive) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  if (index == GWLP_WNDPROC) {
    WNDPROC procedure = (WNDPROC)(uintptr_t)value;
    if (!procedure) {
      local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      return 0;
    }
    WNDPROC previous = record->procedure;
    record->procedure = procedure;
    return (LONG_PTR)(uintptr_t)previous;
  }
  if (index == GWL_ID) {
    const LONG_PTR previous = (LONG_PTR)record->id;
    record->id = (UINT)value;
    return previous;
  }
  LONG_PTR dialogPrevious;
  if (index >= 0 &&
      ui_dialog_window_long(handle, index, TRUE, value, &dialogPrevious))
    return dialogPrevious;
  if (index >= 0 && index + (int)sizeof(LONG) <= record->extraBytes) {
    LONG previous, stored = (LONG)value;
    memcpy(&previous, record->extra + index, sizeof(previous));
    memcpy(record->extra + index, &stored, sizeof(stored));
    return (LONG_PTR)previous;
  }
  if (index >= 0) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_INDEX);
    return 0;
  }
  uint32_t nativeIndex, nativeValue;
  if (index == GWLP_USERDATA) {
    if (value < INT32_MIN ||
        (value > 0 && (uint64_t)value > UINT32_MAX)) {
      local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      return 0;
    }
    nativeIndex = UI_WINDOW_USERDATA;
    nativeValue = (uint32_t)value;
  } else if (index == GWL_STYLE) {
    DWORD style = (DWORD)value;
    if (!valid_window_style(style)) {
      local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
      return 0;
    }
    if ((style & WS_CHILD) != (record->style & WS_CHILD)) {
      local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
      return 0;
    }
    nativeIndex = UI_WINDOW_STYLE;
    nativeValue = style_to_native(style);
  } else {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
  }
  uint32_t previous = 0;
  if (!backend_ok(ui_backend_set_window_long(handle, nativeIndex, nativeValue,
                                               &previous)))
    return 0;
  if (index == GWLP_USERDATA) {
    record->userData = value;
    return (LONG_PTR)(uint32_t)previous;
  }
  DWORD previousStyle = style_from_native(previous);
  if (record->style & WS_CHILD) {
    previousStyle |= record->style & WS_CHILD_EXTRA;
    record->style = (record->style & ~WS_CHILD_EXTRA) |
                    ((DWORD)value & WS_CHILD_EXTRA);
  }
  record->style = (record->style & ~(WS_VISIBLE | WS_BORDER | WS_CAPTION)) |
                  (previousStyle & (WS_VISIBLE | WS_BORDER | WS_CAPTION));
  return (LONG_PTR)previousStyle;
}

LONG GetWindowLong(HWND window, int index) {
  return (LONG)GetWindowLongPtr(window, index);
}

LONG SetWindowLong(HWND window, int index, LONG value) {
  return (LONG)SetWindowLongPtr(window, index, value);
}

BOOL SetWindowText(HWND window, LPCSTR text) {
  if (!utf8_string(text, UI_TITLE_BYTES, NULL, TRUE)) return FALSE;
  uint32_t handle = hwnd_value(window);
  WindowRecord *record = window_by_handle(handle);
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if (ui_controls_is_control(handle)) {
    ui_copy_text(record->title, sizeof(record->title), text);
    return SendMessage(window, WM_SETTEXT, 0, (LPARAM)text) != 0;
  }
  if (!backend_ok(ui_backend_set_window_text(handle, text))) return FALSE;
  ui_copy_text(record->title, sizeof(record->title), text);
  ui_mdi_retitle(handle);
  return TRUE;
}
