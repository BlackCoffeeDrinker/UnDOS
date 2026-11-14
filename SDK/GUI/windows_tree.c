#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/win_internal.h"

/* Parent/child navigation, enable state, focus and window text. Sibling
   order is creation order, which is also the dialog tab order. */

uint32_t ui_focus_window;

static WindowRecord *live(HWND window) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  if (!record || !record->alive) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  return record;
}

HWND GetParent(HWND window) {
  WindowRecord *record = live(window);
  if (!record) return NULL;
  return hwnd_from_value(record->parent);
}

BOOL IsWindow(HWND window) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  return record && record->alive && !record->recoveryOnly;
}

BOOL IsChild(HWND parent, HWND window) {
  const uint32_t target = hwnd_value(parent);
  WindowRecord *record = window_by_handle(hwnd_value(window));
  while (record && record->alive && record->parent) {
    if (record->parent == target) return TRUE;
    record = window_by_handle(record->parent);
  }
  return FALSE;
}

/* Siblings of parent ordered by identity: first, last, next or previous. */
static WindowRecord *sibling(uint32_t parent, uint64_t pivot, int direction) {
  WindowRecord *best = NULL;
  for (size_t i = 0; i < UI_WINDOW_LIMIT; ++i) {
    WindowRecord *candidate = &windows[i];
    if (!candidate->used || !candidate->alive || candidate->recoveryOnly ||
        candidate->parent != parent)
      continue;
    const uint64_t id = candidate->identity;
    if (direction > 0 && id <= pivot) continue;
    if (direction < 0 && id >= pivot) continue;
    if (!best || (direction > 0 ? id < best->identity : id > best->identity))
      best = candidate;
  }
  return best;
}

HWND GetWindow(HWND window, UINT command) {
  WindowRecord *record = live(window);
  if (!record) return NULL;
  WindowRecord *result = NULL;
  switch (command) {
    case GW_HWNDFIRST: result = sibling(record->parent, 0, 1); break;
    case GW_HWNDLAST: result = sibling(record->parent, UINT64_MAX, -1); break;
    case GW_HWNDNEXT: result = sibling(record->parent, record->identity, 1); break;
    case GW_HWNDPREV:
      result = sibling(record->parent, record->identity, -1);
      break;
    case GW_CHILD: result = sibling(record->handle, 0, 1); break;
    case GW_OWNER: return NULL;
    default:
      local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      return NULL;
  }
  return result ? hwnd_from_value(result->handle) : NULL;
}

int GetDlgCtrlID(HWND window) {
  WindowRecord *record = live(window);
  return record ? (int)record->id : 0;
}

BOOL IsWindowEnabled(HWND window) {
  WindowRecord *record = live(window);
  return record && !record->disabled;
}

BOOL EnableWindow(HWND window, BOOL enable) {
  WindowRecord *record = live(window);
  if (!record) return FALSE;
  const BOOL wasDisabled = record->disabled;
  record->disabled = !enable;
  if (wasDisabled != record->disabled) {
    if (record->disabled && ui_focus_window &&
        (ui_focus_window == record->handle ||
         IsChild(window, hwnd_from_value(ui_focus_window))))
      (void)SetFocus(NULL);
    (void)SendMessage(window, WM_ENABLE, enable ? TRUE : FALSE, 0);
    (void)InvalidateRect(window, NULL, TRUE);
  }
  return wasDisabled;
}

HWND GetFocus(void) {
  if (ui_focus_window && !IsWindow(hwnd_from_value(ui_focus_window)))
    ui_focus_window = 0;
  return hwnd_from_value(ui_focus_window);
}

HWND SetFocus(HWND window) {
  const uint32_t handle = hwnd_value(window);
  if (handle && !live(window)) return NULL;
  const HWND previous = GetFocus();
  if (handle == ui_focus_window) return previous;
  ui_focus_window = handle;
  // The compositor may only track top-level focus; keys that reach the
  // top-level window are forwarded to the focused child by the dialog
  // manager. NOT_YET_VERIFIED: kernel child-focus routing.
  (void)ui_backend_set_focus(handle);
  if (previous) (void)SendMessage(previous, WM_KILLFOCUS, (WPARAM)window, 0);
  if (handle && ui_focus_window == handle)
    (void)SendMessage(window, WM_SETFOCUS, (WPARAM)previous, 0);
  return previous;
}

int GetWindowText(HWND window, LPSTR buffer, int size) {
  WindowRecord *record = live(window);
  if (!record) return 0;
  if (!buffer || size <= 0) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (ui_controls_is_control(record->handle))
    return (int)SendMessage(window, WM_GETTEXT, (WPARAM)size, (LPARAM)buffer);
  ui_copy_text(buffer, (size_t)size, record->title);
  return (int)strlen(buffer);
}

int GetWindowTextLength(HWND window) {
  WindowRecord *record = live(window);
  if (!record) return 0;
  if (ui_controls_is_control(record->handle))
    return (int)SendMessage(window, WM_GETTEXTLENGTH, 0, 0);
  return (int)strlen(record->title);
}
