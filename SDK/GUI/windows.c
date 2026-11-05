#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/utf8.h"

#define UI_CLASS_LIMIT 32
#define UI_WINDOW_LIMIT 64
#define UI_DC_LIMIT 16
#define UI_CLASS_NAME_BYTES 31
#define UI_TITLE_BYTES 63
#define UI_FONT_TEXT_BYTES 1024
#define WS_SUPPORTED_CHROME \
  (WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_BORDER))

typedef struct ClassRecord {
  BOOL used;
  ATOM atom;
  char name[UI_CLASS_NAME_BYTES + 1];
  WNDPROC procedure;
  uint32_t background;
} ClassRecord;

typedef struct WindowRecord {
  BOOL used;
  BOOL alive;
  BOOL destroying;
  BOOL recoveryOnly;
  BOOL uiRecovery;
  BOOL hasCleanupFont;
  UNDOS_FONT cleanupFont;
  uint32_t active;
  uint64_t identity;
  uint32_t handle;
  uint32_t parent;
  DWORD setupError;
  DWORD setupStatus;
  DWORD cleanupError;
  DWORD cleanupStatus;
  LONG_PTR userData;
  DWORD style;
  WNDPROC procedure;
  UNDOS_UI_CONTEXT *uiContext;
} WindowRecord;

typedef struct DCRecord {
  BOOL used;
  BOOL ended;
  uint32_t handle;
  uint32_t window;
  UNDOS_FONT currentFont;
  BOOL hasFont;
  UNDOS_FONT defaultFont;
  BOOL hasDefaultFont;
  COLORREF textColor;
  COLORREF backgroundColor;
  BOOL opaque;
} DCRecord;

static ClassRecord classes[UI_CLASS_LIMIT];
static WindowRecord windows[UI_WINDOW_LIMIT];
static DCRecord dcs[UI_DC_LIMIT];
static DWORD lastError;
static DWORD lastStatus;
static BOOL quitPending;
static int quitCode;
static ATOM nextAtom = 1;
static uint64_t nextWindowIdentity = 1;
static uint32_t nearest_palette(COLORREF color);

#ifdef UNDOSGUI_TESTING
void undosgui_test_reset(void) {
  memset(classes, 0, sizeof(classes));
  memset(windows, 0, sizeof(windows));
  memset(dcs, 0, sizeof(dcs));
  lastError = 0;
  lastStatus = 0;
  quitPending = FALSE;
  quitCode = 0;
  nextAtom = 1;
  nextWindowIdentity = 1;
}

#endif

static DWORD error_for_status(uint32_t status) {
  switch (status) {
    case UI_STATUS_OUT_OF_MEMORY: return ERROR_NOT_ENOUGH_MEMORY;
    case UI_STATUS_INVALID_ARGUMENT: return ERROR_INVALID_PARAMETER;
    case UI_STATUS_INVALID_SIZE: return ERROR_INSUFFICIENT_BUFFER;
    case UI_STATUS_INVALID_ADDRESS: return ERROR_NOACCESS;
    case UI_STATUS_UNSUPPORTED: return ERROR_CALL_NOT_IMPLEMENTED;
    case UI_STATUS_TIMEOUT: return ERROR_TIMEOUT;
    case UI_STATUS_RETRY: return ERROR_RETRY;
    case UI_STATUS_NOT_FOUND: return ERROR_NOT_FOUND;
    default: return ERROR_GEN_FAILURE;
  }
}

DWORD ui_core_error_from_status(uint32_t status) {
  return error_for_status(status);
}

static void failure(uint32_t status) {
  lastStatus = status;
  lastError = error_for_status(status);
}

static void local_failure(uint32_t status, DWORD error) {
  lastStatus = status;
  lastError = error;
}

void ui_core_failure(uint32_t status, DWORD error) {
  local_failure(status, error);
}

static BOOL backend_ok(uint32_t status) {
  if (status == UI_STATUS_OK) return TRUE;
  failure(status);
  return FALSE;
}

static BOOL utf8_string(const char *text, size_t limit, size_t *length,
                        BOOL allowEmpty) {
  size_t bytes = 0;
  if (!text) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if ((uintptr_t)text <= UINT16_MAX) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  while (bytes <= limit && text[bytes]) ++bytes;
  if (bytes > limit) {
    local_failure(UI_STATUS_INVALID_SIZE, ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  if (!bytes && !allowEmpty) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!undos_utf8_valid(text, bytes)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_NO_UNICODE_TRANSLATION);
    return FALSE;
  }
  if (length) *length = bytes;
  return TRUE;
}

static ClassRecord *class_by_name(const char *name) {
  for (size_t i = 0; i < UI_CLASS_LIMIT; ++i)
    if (classes[i].used && strcmp(classes[i].name, name) == 0)
      return &classes[i];
  return NULL;
}

static ClassRecord *class_slot(void) {
  for (size_t i = 0; i < UI_CLASS_LIMIT; ++i)
    if (!classes[i].used) return &classes[i];
  return NULL;
}

static WindowRecord *live_window_by_handle(uint32_t handle) {
  for (size_t i = 0; i < UI_WINDOW_LIMIT; ++i)
    if (windows[i].used && windows[i].alive &&
        windows[i].handle == handle) return &windows[i];
  return NULL;
}

static WindowRecord *window_by_handle(uint32_t handle) {
  WindowRecord *record = live_window_by_handle(handle);
  return record && !record->recoveryOnly ? record : NULL;
}

static WindowRecord *window_slot(void) {
  for (size_t i = 0; i < UI_WINDOW_LIMIT; ++i)
    if (!windows[i].used) return &windows[i];
  return NULL;
}

static DCRecord *dc_by_handle(uint32_t handle) {
  for (size_t i = 0; i < UI_DC_LIMIT; ++i)
    if (dcs[i].used && dcs[i].handle == handle) return &dcs[i];
  return NULL;
}

static DCRecord *dc_slot(void) {
  for (size_t i = 0; i < UI_DC_LIMIT; ++i)
    if (!dcs[i].used) return &dcs[i];
  return NULL;
}

#ifdef UNDOSGUI_TESTING
void undosgui_test_track_dc(uint32_t handle, uint32_t window) {
  DCRecord *record = dc_slot();
  if (!record) return;
  memset(record, 0, sizeof(*record));
  record->used = TRUE;
  record->handle = handle;
  record->window = window;
  record->textColor = RGB(0, 0, 0);
  record->backgroundColor = UndosPaletteColor(7);
  record->opaque = TRUE;
}
#endif

static uint32_t hwnd_value(HWND window) {
  return (uint32_t)(uintptr_t)window;
}

static uint32_t hdc_value(HDC dc) {
  return (uint32_t)(uintptr_t)dc;
}

static HWND hwnd_from_value(uint32_t window) {
  return (HWND)(uintptr_t)window;
}

static HDC hdc_from_value(uint32_t dc) {
  return (HDC)(uintptr_t)dc;
}

static uint32_t style_to_native(DWORD style) {
  uint32_t native = 0;
  if (style & WS_VISIBLE) native |= UI_WINDOW_VISIBLE;
  if (style & WS_CAPTION) native |= UI_WINDOW_CAPTION;
  if ((style & WS_BORDER) || (style & WS_CAPTION))
    native |= UI_WINDOW_BORDER;
  if (style & WS_CHILD) native |= UI_WINDOW_CHILD;
  return native;
}

static DWORD style_from_native(uint32_t style) {
  DWORD result = 0;
  if (style & UI_WINDOW_VISIBLE) result |= WS_VISIBLE;
  if (style & UI_WINDOW_CHILD) result |= WS_CHILD;
  if (style & UI_WINDOW_CAPTION) result |= WS_CAPTION;
  else if (style & UI_WINDOW_BORDER) result |= WS_BORDER;
  return result;
}

static BOOL valid_window_style(DWORD style) {
  const DWORD supported = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_CAPTION |
                          WS_OVERLAPPEDWINDOW;
  const DWORD chrome = style & WS_SUPPORTED_CHROME;
  return !(style & ~supported) &&
         (!chrome || chrome == WS_SUPPORTED_CHROME);
}

static BOOL translate_show(int command, uint32_t *native) {
  switch (command) {
    case SW_HIDE: *native = UI_SHOW_HIDE; return TRUE;
    case SW_SHOWNORMAL:
    case SW_RESTORE: *native = UI_SHOW_RESTORE; return TRUE;
    case SW_SHOWMINIMIZED:
    case SW_MINIMIZE: *native = UI_SHOW_MINIMIZE; return TRUE;
    case SW_MAXIMIZE: *native = UI_SHOW_MAXIMIZE; return TRUE;
    case SW_SHOW: *native = UI_SHOW_SHOW; return TRUE;
    default:
      local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
      return FALSE;
  }
}

static BOOL backend_dc_colors(DCRecord *dc) {
  const uint32_t foreground = nearest_palette(dc->textColor);
  const uint32_t background = nearest_palette(dc->backgroundColor);
  return backend_ok(ui_backend_set_draw_colors(dc->handle, foreground,
                                                background, dc->opaque));
}

static void forget_window_dcs(uint32_t handle);

static void forget_window(WindowRecord *window) {
  if (window->uiContext && window->uiContext->_sdkBorrowers) {
    --window->uiContext->_sdkBorrowers;
    window->uiContext = NULL;
  }
  if (window->active) {
    window->alive = FALSE;
    window->destroying = FALSE;
    return;
  }
  const uint32_t handle = window->handle;
  window->used = FALSE;
  forget_window_dcs(handle);
}

static BOOL cleanup_window_dcs(uint32_t handle) {
  for (size_t i = 0; i < UI_DC_LIMIT; ++i) {
    if (dcs[i].used && dcs[i].window == handle) {
      if (!dcs[i].ended) {
        uint32_t status = ui_backend_end_paint(dcs[i].window, dcs[i].handle);
        if (status != UI_STATUS_OK) {
          failure(status);
          return FALSE;
        }
        dcs[i].ended = TRUE;
      }
      if (dcs[i].hasDefaultFont) {
        uint32_t status = ui_backend_close_font(dcs[i].defaultFont);
        if (status != UI_STATUS_OK) {
          failure(status);
          return FALSE;
        }
        dcs[i].hasDefaultFont = FALSE;
      }
      dcs[i].used = FALSE;
    }
  }
  return TRUE;
}

static void forget_window_dcs(uint32_t handle) {
  for (size_t i = 0; i < UI_DC_LIMIT; ++i)
    if (dcs[i].used && dcs[i].window == handle)
      dcs[i].ended = TRUE;
  (void)cleanup_window_dcs(handle);
}

static void forget_descendants(uint32_t parent) {
  for (size_t i = 0; i < UI_WINDOW_LIMIT; ++i) {
    WindowRecord *child = &windows[i];
    if (child->used && child->alive && child->parent == parent) {
      const uint32_t handle = child->handle;
      forget_window(child);
      forget_descendants(handle);
    }
  }
}

DWORD GetLastError(void) {
  return lastError;
}

void SetLastError(DWORD error) {
  lastError = error;
}

DWORD UndosGetLastStatus(void) {
  return lastStatus;
}

BOOL UndosGetWindowCreationRecovery(
    UNDOS_WINDOW_CREATION_RECOVERY *recovery) {
  if (!recovery) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  for (size_t i = 0; i < UI_WINDOW_LIMIT; ++i) {
    const WindowRecord *record = &windows[i];
    if (!record->used || !record->alive || !record->recoveryOnly ||
        record->uiRecovery) continue;
    UNDOS_WINDOW_CREATION_RECOVERY result = {
        hwnd_from_value(record->handle), record->setupError,
        record->setupStatus, record->cleanupError, record->cleanupStatus};
    *recovery = result;
    return TRUE;
  }
  return FALSE;
}

BOOL UndosGetUiCleanupRecovery(UNDOS_UI_CLEANUP_RECOVERY *recovery) {
  if (!recovery) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  for (size_t i = 0; i < UI_WINDOW_LIMIT; ++i) {
    const WindowRecord *record = &windows[i];
    if (!record->used || !record->alive || !record->uiRecovery) continue;
    UNDOS_UI_CLEANUP_RECOVERY result = {
        hwnd_from_value(record->handle), record->setupError,
        record->setupStatus, record->cleanupError, record->cleanupStatus};
    *recovery = result;
    return TRUE;
  }
  return FALSE;
}

int ui_core_window_alive(HWND window) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  return record && record->alive;
}

int ui_core_window_in_family(HWND window, HWND owner) {
  uint32_t current = hwnd_value(window);
  const uint32_t root = hwnd_value(owner);
  for (size_t depth = 0; depth < UI_WINDOW_LIMIT && current; ++depth) {
    if (current == root) return 1;
    WindowRecord *record = window_by_handle(current);
    if (!record) return 0;
    current = record->parent;
  }
  return 0;
}

uint32_t ui_core_window_parent(HWND window) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  return record ? record->parent : 0;
}

UNDOS_UI_CONTEXT *ui_core_window_context(HWND window) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  return record ? record->uiContext : NULL;
}

int ui_core_attach_context(HWND window, UNDOS_UI_CONTEXT *context) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  if ((record->uiContext && record->uiContext->activeDepth) ||
      (context && (context->_sdkState != UI_CONTEXT_MAGIC ||
                   context->activeDepth))) {
    local_failure(UI_STATUS_RETRY, ERROR_RETRY);
    return 0;
  }
  if (record->uiContext == context) return 1;
  if (record->uiContext && record->uiContext->_sdkBorrowers)
    --record->uiContext->_sdkBorrowers;
  record->uiContext = context;
  if (context) ++context->_sdkBorrowers;
  return 1;
}

int ui_core_mark_ui_recovery(HWND window, DWORD operationError,
                             DWORD operationStatus, DWORD cleanupError,
                             DWORD cleanupStatus) {
  WindowRecord *record = live_window_by_handle(hwnd_value(window));
  if (!record) return 0;
  if (record->uiContext && record->uiContext->_sdkBorrowers)
    --record->uiContext->_sdkBorrowers;
  record->uiContext = NULL;
  record->recoveryOnly = TRUE;
  record->uiRecovery = TRUE;
  record->setupError = operationError;
  record->setupStatus = operationStatus;
  record->cleanupError = cleanupError;
  record->cleanupStatus = cleanupStatus;
  return 1;
}

int ui_core_retain_font(HWND window, UNDOS_FONT font) {
  WindowRecord *record = live_window_by_handle(hwnd_value(window));
  if (!record || record->hasCleanupFont) {
    local_failure(UI_STATUS_RETRY, ERROR_RETRY);
    return 0;
  }
  record->cleanupFont = font;
  record->hasCleanupFont = TRUE;
  return 1;
}

int ui_core_dc_uses_font(HDC dc, UNDOS_FONT font) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record || !record->hasFont || record->currentFont != font) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return 0;
  }
  return 1;
}

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
  if (windowClass->style || windowClass->cbClsExtra || windowClass->cbWndExtra ||
      windowClass->hIcon || windowClass->hCursor || windowClass->lpszMenuName) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
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
  if (menu && (style & WS_CHILD)) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
  }
  if (x == CW_USEDEFAULT) x = 0;
  if (y == CW_USEDEFAULT) y = 0;
  if (width == CW_USEDEFAULT) width = 320;
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
  if (width <= 0 || height <= 0) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
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
  HWND created = hwnd_from_value(handle);
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
  if (!backend_ok(ui_backend_move_window(handle, x, y, width, height)))
    return FALSE;
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
  if (index == GWL_STYLE) {
    uint32_t native = 0;
    if (!backend_ok(ui_backend_get_window_long(handle, UI_WINDOW_STYLE, &native)))
      return 0;
    return (LONG_PTR)style_from_native(native);
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
  if (!window_by_handle(handle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  return backend_ok(ui_backend_set_window_text(handle, text));
}

HMENU CreateMenu(void) {
  uint32_t menu = 0;
  if (!backend_ok(ui_backend_create_menu(0, &menu))) return NULL;
  return (HMENU)(uintptr_t)menu;
}

HMENU CreatePopupMenu(void) {
  uint32_t menu = 0;
  if (!backend_ok(ui_backend_create_menu(1, &menu))) return NULL;
  return (HMENU)(uintptr_t)menu;
}

BOOL DestroyMenu(HMENU menu) {
  return backend_ok(ui_backend_destroy_menu((uint32_t)(uintptr_t)menu));
}

BOOL AppendMenu(HMENU menu, UINT flags, UINT_PTR item, LPCSTR text) {
  const UINT allowed = MF_GRAYED | MF_DISABLED | MF_POPUP | MF_SEPARATOR;
  if (flags & ~allowed) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
  }
  if ((flags & MF_SEPARATOR) && (flags & (MF_POPUP | MF_DISABLED | MF_GRAYED))) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if ((flags & MF_POPUP) && item > UINT32_MAX) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (item > UINT32_MAX) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!(flags & MF_SEPARATOR) && !utf8_string(text, UI_TITLE_BYTES, NULL, TRUE))
    return FALSE;
  if (flags & MF_SEPARATOR) text = "";
  return backend_ok(ui_backend_append_menu((uint32_t)(uintptr_t)menu, flags,
                                            (uint32_t)item, text));
}

BOOL SetMenu(HWND window, HMENU menu) {
  uint32_t handle = hwnd_value(window);
  if (!window_by_handle(handle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  return backend_ok(ui_backend_set_menu(handle, (uint32_t)(uintptr_t)menu));
}

HMENU GetMenu(HWND window) {
  uint32_t menu = 0;
  if (!window_by_handle(hwnd_value(window))) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  if (!backend_ok(ui_backend_get_menu(hwnd_value(window), &menu))) return NULL;
  return (HMENU)(uintptr_t)menu;
}

static void copy_message(MSG *destination, const UiMessage *source) {
  destination->hwnd = hwnd_from_value(source->hwnd);
  destination->message = source->message;
  destination->wParam = source->wParam;
  destination->lParam = (LPARAM)(int32_t)source->lParam;
  destination->time = source->time;
  destination->pt.x = 0;
  destination->pt.y = 0;
}

static void quit_message(MSG *message) {
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
  } else if (!backend_ok(ui_backend_get_message(&native))) {
    return -1;
  }
  copy_message(message, &native);
  if (message->message == WM_QUIT) {
    quitPending = FALSE;
    return 0;
  }
  return 1;
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
    if (!quitPending) return FALSE;
    quit_message(message);
    if (remove == PM_REMOVE) quitPending = FALSE;
    return TRUE;
  }
  if (!backend_ok(status)) return FALSE;
  copy_message(message, &native);
  if (remove == PM_REMOVE && message->message == WM_QUIT) quitPending = FALSE;
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

LRESULT DispatchMessage(const MSG *message) {
  if (!message || message->message == WM_QUIT || !message->hwnd) return 0;
  WindowRecord *record = window_by_handle(hwnd_value(message->hwnd));
  if (!record || !record->alive) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  if (message->message == WM_DISPLAYCHANGE && record->uiContext &&
      record->uiContext->hasDisplayDc)
    record->uiContext->displayDcStale = TRUE;
  uint32_t ignored;
  if (!backend_ok(ui_backend_get_window_long(record->handle,
                                              UI_WINDOW_USERDATA, &ignored)))
    return 0;
  WNDPROC procedure = record->procedure;
  if (!procedure) return 0;
  ++record->active;
  LRESULT result = procedure(message->hwnd, message->message,
                             message->wParam, message->lParam);
  --record->active;
  if (!record->active && !record->alive) {
    uint32_t handle = record->handle;
    record->used = FALSE;
    forget_window_dcs(handle);
  }
  return result;
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
  if (message == WM_CLOSE) (void)DestroyWindow(window);
  return 0;
}

BOOL TranslateMessage(const MSG *message) {
  (void)message;
  return FALSE;
}

HDC BeginPaint(HWND window, PAINTSTRUCT *paint) {
  if (!paint) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  uint32_t windowHandle = hwnd_value(window);
  if (!window_by_handle(windowHandle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  DCRecord *record = dc_slot();
  if (!record) {
    local_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  uint32_t handle = 0;
  UiPaintInfo info = {0};
  uint32_t status = ui_backend_begin_paint(windowHandle, &handle, &info);
  if (!backend_ok(status)) return NULL;
  if (!handle) {
    uint32_t cleanup = ui_backend_end_paint(windowHandle, handle);
    (void)cleanup;
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return NULL;
  }
  record->used = TRUE;
  record->ended = FALSE;
  record->handle = handle;
  record->window = windowHandle;
  record->hasFont = FALSE;
  record->hasDefaultFont = FALSE;
  record->textColor = RGB(0, 0, 0);
  record->backgroundColor = UndosPaletteColor(7);
  record->opaque = TRUE;

  UNDOS_FONT font;
  uint32_t generation;
  if (!UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT,
                        UNDOS_GLYPH_FALLBACK_NOTDEF, &font, &generation)) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    (void)ui_backend_end_paint(windowHandle, handle);
    record->used = FALSE;
    local_failure(raw, error);
    return NULL;
  }
  (void)generation;
  uint32_t previous;
  status = ui_backend_select_font(handle, font, &previous);
  if (!backend_ok(status)) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    (void)ui_backend_close_font(font);
    (void)ui_backend_end_paint(windowHandle, handle);
    record->used = FALSE;
    local_failure(raw, error);
    return NULL;
  }
  record->currentFont = font;
  record->hasFont = TRUE;
  record->defaultFont = font;
  record->hasDefaultFont = TRUE;

  PAINTSTRUCT result = {0};
  result.hdc = hdc_from_value(handle);
  result.fErase = info.erased != 0;
  result.rcPaint.left = info.rcPaint.left;
  result.rcPaint.top = info.rcPaint.top;
  result.rcPaint.right = info.rcPaint.right;
  result.rcPaint.bottom = info.rcPaint.bottom;
  *paint = result;
  return result.hdc;
}

BOOL EndPaint(HWND window, const PAINTSTRUCT *paint) {
  if (!paint || !paint->hdc) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  uint32_t handle = hdc_value(paint->hdc);
  DCRecord *record = dc_by_handle(handle);
  if (!record || record->window != hwnd_value(window)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT,
                  record ? ERROR_INVALID_WINDOW_HANDLE : ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (!record->ended) {
    if (!backend_ok(ui_backend_end_paint(record->window, record->handle)))
      return FALSE;
    record->ended = TRUE;
  }
  uint32_t closeStatus = UI_STATUS_OK;
  if (record->hasDefaultFont)
    closeStatus = ui_backend_close_font(record->defaultFont);
  if (closeStatus != UI_STATUS_OK) {
    failure(closeStatus);
    return FALSE;
  }
  record->hasDefaultFont = FALSE;
  record->used = FALSE;
  return TRUE;
}

COLORREF UndosPaletteColor(UINT index) {
  static const uint32_t rgb[16] = {
      0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
      0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
      0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
      0xff5555, 0xff55ff, 0xffff55, 0xffffff};
  if (index > 15) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return CLR_INVALID;
  }
  uint32_t color = rgb[index];
  return RGB((color >> 16) & 255, (color >> 8) & 255, color & 255);
}

static uint32_t nearest_palette(COLORREF color) {
  static const uint32_t rgb[16] = {
      0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
      0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
      0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
      0xff5555, 0xff55ff, 0xffff55, 0xffffff};
  const int red = color & 255;
  const int green = (color >> 8) & 255;
  const int blue = (color >> 16) & 255;
  uint32_t best = 0;
  uint32_t bestDistance = UINT32_MAX;
  for (uint32_t i = 0; i < 16; ++i) {
    const int dr = red - (int)((rgb[i] >> 16) & 255);
    const int dg = green - (int)((rgb[i] >> 8) & 255);
    const int db = blue - (int)(rgb[i] & 255);
    const uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
    if (distance < bestDistance) {
      best = i;
      bestDistance = distance;
    }
  }
  return best;
}

static BOOL valid_color(COLORREF color) {
  if (color & 0xFF000000u) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return TRUE;
}

static COLORREF set_dc_color(HDC handle, COLORREF color, BOOL foreground) {
  DCRecord *record = dc_by_handle(hdc_value(handle));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return CLR_INVALID;
  }
  if (!valid_color(color)) return CLR_INVALID;
  COLORREF previous = foreground ? record->textColor : record->backgroundColor;
  if (foreground) record->textColor = color;
  else record->backgroundColor = color;
  if (!backend_dc_colors(record)) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    if (foreground) record->textColor = previous;
    else record->backgroundColor = previous;
    local_failure(raw, error);
    return CLR_INVALID;
  }
  return previous;
}

COLORREF SetTextColor(HDC dc, COLORREF color) {
  return set_dc_color(dc, color, TRUE);
}

COLORREF SetBkColor(HDC dc, COLORREF color) {
  return set_dc_color(dc, color, FALSE);
}

int SetBkMode(HDC dc, int mode) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return 0;
  }
  if (mode != OPAQUE && mode != TRANSPARENT) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  int previous = record->opaque ? OPAQUE : TRANSPARENT;
  record->opaque = mode == OPAQUE;
  if (!backend_dc_colors(record)) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    record->opaque = previous == OPAQUE;
    local_failure(raw, error);
    return 0;
  }
  return previous;
}

BOOL UndosAcquireFont(enum UndosFontSource source,
                      enum UndosGlyphFallback fallback, UNDOS_FONT *font,
                      uint32_t *generation) {
  if (!font || !generation ||
      (source != UNDOS_FONT_SYSTEM_DEFAULT &&
       source != UNDOS_FONT_BUILTIN_BITMAP) ||
      (fallback != UNDOS_GLYPH_FALLBACK_NOTDEF &&
       fallback != UNDOS_GLYPH_FALLBACK_BITMAP)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  uint32_t value = 0, version = 0;
  uint32_t status = ui_backend_acquire_font(source, fallback, &value, &version);
  if (!backend_ok(status)) return FALSE;
  *font = value;
  *generation = version;
  return TRUE;
}

BOOL UndosCloseFont(UNDOS_FONT font) {
  return backend_ok(ui_backend_close_font(font));
}

BOOL UndosSelectFont(HDC dc, UNDOS_FONT font, UNDOS_FONT *previous,
                     BOOL *hadPrevious) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (!previous || !hadPrevious) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  uint32_t old = 0;
  uint32_t status = ui_backend_select_font(record->handle, font, &old);
  if (!backend_ok(status)) return FALSE;
  const BOOL hadOld = record->hasFont;
  const UNDOS_FONT oldFont = (UNDOS_FONT)old;
  if (record->hasDefaultFont && hadOld &&
      record->defaultFont == oldFont)
    record->hasDefaultFont = FALSE;
  record->currentFont = font;
  record->hasFont = TRUE;
  *previous = oldFont;
  *hadPrevious = hadOld;
  return TRUE;
}

BOOL UndosGetFontMetrics(UNDOS_FONT font, enum UndosTextStyle style,
                         enum UndosTextRepertoire repertoire,
                         UNDOS_TEXT_METRICS *metrics) {
  if (!metrics || (style != UNDOS_TEXT_REGULAR && style != UNDOS_TEXT_BOLD) ||
      (repertoire != UNDOS_TEXT_BASIC && repertoire != UNDOS_TEXT_TERMINAL)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  UiTextMetrics value;
  if (!backend_ok(ui_backend_get_font_metrics(font, style, repertoire, &value)))
    return FALSE;
  UNDOS_TEXT_METRICS result = {
      value.generation, value.ascent, value.descent, value.lineGap,
      value.lineHeight, value.baselineOffset, value.cellWidth, value.cellHeight,
      value.inkLeft, value.inkTop, value.inkRight, value.inkBottom};
  *metrics = result;
  return TRUE;
}

BOOL UndosMeasureText(UNDOS_FONT font, enum UndosTextStyle style,
                      enum UndosTextLayout layout,
                      enum UndosTextRepertoire repertoire, LPCSTR text,
                      UNDOS_TEXT_EXTENT *extent) {
  if (!extent || (style != UNDOS_TEXT_REGULAR && style != UNDOS_TEXT_BOLD) ||
      (layout != UNDOS_TEXT_PROPORTIONAL &&
       layout != UNDOS_TEXT_UNIFORM_GRID) ||
      (repertoire != UNDOS_TEXT_BASIC && repertoire != UNDOS_TEXT_TERMINAL)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!utf8_string(text, UI_FONT_TEXT_BYTES, NULL, TRUE)) return FALSE;
  UiTextExtent value;
  if (!backend_ok(ui_backend_measure_text(font, style, layout, repertoire,
                                          text, &value)))
    return FALSE;
  UNDOS_TEXT_EXTENT result = {
      value.generation, value.advanceWidth, value.lineHeight,
      value.baselineOffset, value.inkLeft, value.inkTop,
      value.inkRight, value.inkBottom};
  *extent = result;
  return TRUE;
}

BOOL UndosDrawText(HDC dc, int x, int y, enum UndosTextStyle style,
                   enum UndosTextLayout layout,
                   enum UndosTextRepertoire repertoire, LPCSTR text) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if ((style != UNDOS_TEXT_REGULAR && style != UNDOS_TEXT_BOLD) ||
      (layout != UNDOS_TEXT_PROPORTIONAL &&
       layout != UNDOS_TEXT_UNIFORM_GRID) ||
      (repertoire != UNDOS_TEXT_BASIC && repertoire != UNDOS_TEXT_TERMINAL)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!utf8_string(text, UI_FONT_TEXT_BYTES, NULL, TRUE)) return FALSE;
  return backend_dc_colors(record) &&
      backend_ok(ui_backend_draw_text(record->handle, x, y, style, layout,
                                      repertoire, text));
}

BOOL UndosFillRectangle(HDC dc, int left, int top, int right, int bottom) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return backend_dc_colors(record) &&
      backend_ok(ui_backend_fill_rectangle(record->handle, left, top,
                                            right, bottom));
}

BOOL UndosMoveTo(HDC dc, int x, int y) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return backend_ok(ui_backend_move_to(record->handle, x, y));
}

BOOL UndosLineTo(HDC dc, int x, int y) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return backend_dc_colors(record) &&
      backend_ok(ui_backend_line_to(record->handle, x, y));
}
