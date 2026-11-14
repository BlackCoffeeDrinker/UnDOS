#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/utf8.h"
#include "private/win_internal.h"

ClassRecord classes[UI_CLASS_LIMIT];
WindowRecord windows[UI_WINDOW_LIMIT];
DCRecord dcs[UI_DC_LIMIT];
DWORD lastError;
DWORD lastStatus;
BOOL quitPending;
int quitCode;
ATOM nextAtom = 1;
uint64_t nextWindowIdentity = 1;
BYTE keyState[256];


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
  memset(keyState, 0, sizeof(keyState));
  ui_gdi_reset();
  ui_bitmap_reset();
  ui_resource_reset();
  ui_controls_reset();
  ui_dialog_reset();
  ui_input_reset();
  ui_file_reset();
  ui_shell_reset();
  ui_mdi_reset();
}

#endif

DWORD error_for_status(uint32_t status) {
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

void failure(uint32_t status) {
  lastStatus = status;
  lastError = error_for_status(status);
}

void local_failure(uint32_t status, DWORD error) {
  lastStatus = status;
  lastError = error;
}

void ui_core_failure(uint32_t status, DWORD error) {
  local_failure(status, error);
}

BOOL backend_ok(uint32_t status) {
  if (status == UI_STATUS_OK) return TRUE;
  failure(status);
  return FALSE;
}

BOOL utf8_string(const char *text, size_t limit, size_t *length,
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

ClassRecord *class_by_name(const char *name) {
  for (size_t i = 0; i < UI_CLASS_LIMIT; ++i)
    if (classes[i].used && strcmp(classes[i].name, name) == 0)
      return &classes[i];
  return NULL;
}

ClassRecord *class_slot(void) {
  for (size_t i = 0; i < UI_CLASS_LIMIT; ++i)
    if (!classes[i].used) return &classes[i];
  return NULL;
}

WindowRecord *live_window_by_handle(uint32_t handle) {
  for (size_t i = 0; i < UI_WINDOW_LIMIT; ++i)
    if (windows[i].used && windows[i].alive &&
        windows[i].handle == handle) return &windows[i];
  return NULL;
}

WindowRecord *window_by_handle(uint32_t handle) {
  WindowRecord *record = live_window_by_handle(handle);
  return record && !record->recoveryOnly ? record : NULL;
}

WindowRecord *window_slot(void) {
  for (size_t i = 0; i < UI_WINDOW_LIMIT; ++i)
    if (!windows[i].used) return &windows[i];
  return NULL;
}

DCRecord *dc_by_handle(uint32_t handle) {
  for (size_t i = 0; i < UI_DC_LIMIT; ++i)
    if (dcs[i].used && dcs[i].handle == handle) return &dcs[i];
  return NULL;
}

DCRecord *dc_slot(void) {
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

uint32_t hwnd_value(HWND window) {
  return (uint32_t)(uintptr_t)window;
}

uint32_t hdc_value(HDC dc) {
  return (uint32_t)(uintptr_t)dc;
}

HWND hwnd_from_value(uint32_t window) {
  return (HWND)(uintptr_t)window;
}

HDC hdc_from_value(uint32_t dc) {
  return (HDC)(uintptr_t)dc;
}

uint32_t style_to_native(DWORD style) {
  uint32_t native = 0;
  if (style & WS_VISIBLE) native |= UI_WINDOW_VISIBLE;
  // WS_CAPTION contains the WS_BORDER bit; a border alone has no caption.
  if ((style & WS_CAPTION) == WS_CAPTION) native |= UI_WINDOW_CAPTION;
  if ((style & WS_BORDER) || (style & WS_CAPTION))
    native |= UI_WINDOW_BORDER;
  if (style & WS_CHILD) native |= UI_WINDOW_CHILD;
  return native;
}

DWORD style_from_native(uint32_t style) {
  DWORD result = 0;
  if (style & UI_WINDOW_VISIBLE) result |= WS_VISIBLE;
  if (style & UI_WINDOW_CHILD) result |= WS_CHILD;
  if (style & UI_WINDOW_CAPTION) result |= WS_CAPTION;
  else if (style & UI_WINDOW_BORDER) result |= WS_BORDER;
  return result;
}

BOOL valid_window_style(DWORD style) {
  if (style & WS_CHILD) style &= ~WS_CHILD_EXTRA;
  // The kernel always clips a window's drawing to its visible region, so the
  // clip styles are accepted and need no native bit.
  const DWORD supported = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_CAPTION |
                          WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN |
                          WS_CLIPSIBLINGS;
  const DWORD chrome = style & WS_SUPPORTED_CHROME;
  return !(style & ~supported) &&
         (!chrome || chrome == WS_SUPPORTED_CHROME);
}

BOOL translate_show(int command, uint32_t *native) {
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

BOOL backend_dc_colors(DCRecord *dc) {
  const uint32_t foreground = nearest_palette(dc->textColor);
  const uint32_t background = nearest_palette(dc->backgroundColor);
  return backend_ok(ui_backend_set_draw_colors(dc->handle, foreground,
                                                background, dc->opaque));
}

void forget_window_dcs(uint32_t handle);

void forget_window(WindowRecord *window) {
  if (window->uiContext && window->uiContext->_sdkBorrowers) {
    --window->uiContext->_sdkBorrowers;
    window->uiContext = NULL;
  }
  ui_controls_forget(window->handle);
  ui_dialog_forget(window->handle);
  ui_mdi_forget(window->handle);
  ui_input_forget(window->handle);
  if (ui_focus_window == window->handle) ui_focus_window = 0;
  if (window->active) {
    window->alive = FALSE;
    window->destroying = FALSE;
    return;
  }
  const uint32_t handle = window->handle;
  window->used = FALSE;
  forget_window_dcs(handle);
}

void ui_copy_text(char *destination, size_t capacity, const char *source) {
  size_t i = 0;
  if (!capacity) return;
  for (; source && source[i] && i + 1 < capacity; ++i)
    destination[i] = source[i];
  destination[i] = '\0';
}

BOOL cleanup_window_dcs(uint32_t handle) {
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

void forget_window_dcs(uint32_t handle) {
  for (size_t i = 0; i < UI_DC_LIMIT; ++i)
    if (dcs[i].used && dcs[i].window == handle)
      dcs[i].ended = TRUE;
  (void)cleanup_window_dcs(handle);
}

void forget_descendants(uint32_t parent) {
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
