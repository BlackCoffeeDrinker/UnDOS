#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/controls.h"
#include "private/ui_internal.h"
#include "private/win_internal.h"

/* Dialog manager: DLGTEMPLATE/DLGTEMPLATEEX parsing, modal and modeless
   dialogs, DefDlgProc and keyboard navigation. */

#define UI_DIALOG_LIMIT 8
#define UI_DIALOG_TEXT_BYTES UI_CONTROL_TEXT_BYTES

typedef struct DialogRecord {
  BOOL used;
  uint32_t handle;
  DLGPROC procedure;
  LONG_PTR msgResult;
  LONG_PTR user;
  BOOL modal;
  BOOL ended;
  INT_PTR result;
  int defaultId;
  uint32_t lastFocus;
} DialogRecord;

static DialogRecord dialogs[UI_DIALOG_LIMIT];

void ui_dialog_reset(void) {
  memset(dialogs, 0, sizeof(dialogs));
}

static DialogRecord *dialog_record(HWND window) {
  const uint32_t handle = hwnd_value(window);
  if (!handle) return NULL;
  for (size_t i = 0; i < UI_DIALOG_LIMIT; ++i)
    if (dialogs[i].used && dialogs[i].handle == handle) return &dialogs[i];
  return NULL;
}

void ui_dialog_forget(uint32_t handle) {
  DialogRecord *record = dialog_record(hwnd_from_value(handle));
  if (record) record->used = FALSE;
}

BOOL ui_dialog_window_long(uint32_t handle, int index, BOOL set,
                           LONG_PTR value, LONG_PTR *previous) {
  DialogRecord *record = dialog_record(hwnd_from_value(handle));
  if (!record) return FALSE;
  LONG_PTR *slot;
  LONG_PTR procedure = (LONG_PTR)(uintptr_t)record->procedure;
  switch (index) {
    case DWLP_MSGRESULT: slot = &record->msgResult; break;
    case DWLP_USER: slot = &record->user; break;
    case DWLP_DLGPROC: slot = &procedure; break;
    default: return FALSE;
  }
  *previous = *slot;
  if (set) {
    *slot = value;
    if (index == DWLP_DLGPROC) record->procedure = (DLGPROC)(uintptr_t)value;
  }
  return TRUE;
}

/* Dialog base units come from the system font cell. */
static void base_units(LONG *x, LONG *y) {
  UNDOS_FONT font;
  uint32_t generation;
  UNDOS_TEXT_METRICS metrics;
  const DWORD error = GetLastError();
  *x = 8;
  *y = 16;
  if (UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT, UNDOS_GLYPH_FALLBACK_NOTDEF,
                       &font, &generation)) {
    if (UndosGetFontMetrics(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_BASIC,
                            &metrics)) {
      if (metrics.cellWidth) *x = (LONG)metrics.cellWidth;
      if (metrics.lineHeight) *y = (LONG)metrics.lineHeight;
    }
    (void)UndosCloseFont(font);
  }
  SetLastError(error);
}

LONG GetDialogBaseUnits(void) {
  LONG x, y;
  base_units(&x, &y);
  return MAKELONG(x, y);
}

static void map_rect(RECT *rect) {
  LONG x, y;
  base_units(&x, &y);
  rect->left = rect->left * x / 4;
  rect->right = rect->right * x / 4;
  rect->top = rect->top * y / 8;
  rect->bottom = rect->bottom * y / 8;
}

BOOL MapDialogRect(HWND dialog, RECT *rect) {
  if (!dialog_record(dialog)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if (!rect) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  map_rect(rect);
  return TRUE;
}

/* Template reading. All fields are little-endian WORD units. */

typedef struct Reader {
  const BYTE *base;
  const BYTE *at;
} Reader;

static WORD read_word(Reader *r) {
  const WORD value = (WORD)(r->at[0] | (r->at[1] << 8));
  r->at += 2;
  return value;
}

static DWORD read_dword(Reader *r) {
  const DWORD low = read_word(r);
  return low | ((DWORD)read_word(r) << 16);
}

static void align_dword(Reader *r) {
  const size_t offset = (size_t)(r->at - r->base);
  r->at += (4 - offset % 4) % 4;
}

/* sz_Or_Ord: 0x0000 empty, 0xFFFF ordinal, else UTF-16 text to UTF-8. */
static void read_text(Reader *r, char *out, size_t capacity, WORD *ordinal) {
  if (ordinal) *ordinal = 0;
  if (out && capacity) out[0] = '\0';
  WORD first = read_word(r);
  if (first == 0xFFFFu) {
    const WORD value = read_word(r);
    if (ordinal) *ordinal = value;
    return;
  }
  size_t used = 0;
  for (WORD c = first; c; c = read_word(r)) {
    char bytes[3];
    size_t n;
    if (c < 0x80) {
      bytes[0] = (char)c;
      n = 1;
    } else if (c < 0x800) {
      bytes[0] = (char)(0xC0 | (c >> 6));
      bytes[1] = (char)(0x80 | (c & 0x3F));
      n = 2;
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      bytes[0] = '?';
      n = 1;
    } else {
      bytes[0] = (char)(0xE0 | (c >> 12));
      bytes[1] = (char)(0x80 | ((c >> 6) & 0x3F));
      bytes[2] = (char)(0x80 | (c & 0x3F));
      n = 3;
    }
    if (out && used + n < capacity) {
      memcpy(out + used, bytes, n);
      used += n;
      out[used] = '\0';
    }
  }
}

static const char *atom_class(WORD atom) {
  switch (atom) {
    case 0x0080: return WC_BUTTON;
    case 0x0081: return WC_EDIT;
    case 0x0082: return WC_STATIC;
    case 0x0083: return WC_LISTBOX;
    case 0x0084: return WC_SCROLLBAR;
    default: return NULL;
  }
}

static HWND first_tab_item(HWND dialog);

static BOOL create_item(HWND dialog, const char *className, const char *text,
                        DWORD style, RECT *rect, UINT id) {
  map_rect(rect);
  const size_t length = strlen(text);
  const BOOL shortText = length <= UI_TITLE_BYTES;
  style = (style | WS_CHILD) & ~(DWORD)(WS_POPUP | WS_CAPTION);
  HWND item = CreateWindow(className, shortText ? text : "", style,
                           rect->left, rect->top, rect->right - rect->left,
                           rect->bottom - rect->top, dialog,
                           (HMENU)(uintptr_t)id, NULL, NULL);
  if (!item) return FALSE;
  if (!shortText) (void)SendMessage(item, WM_SETTEXT, 0, (LPARAM)text);
  return TRUE;
}

static HWND create_dialog(LPCDLGTEMPLATE dialogTemplate, HWND owner,
                          DLGPROC procedure, LPARAM param, BOOL modal) {
  if (!dialogTemplate) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (owner && !IsWindow(owner)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  DialogRecord *record = NULL;
  for (size_t i = 0; i < UI_DIALOG_LIMIT && !record; ++i)
    if (!dialogs[i].used) record = &dialogs[i];
  if (!record) {
    local_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  Reader r = {(const BYTE *)dialogTemplate, (const BYTE *)dialogTemplate};
  const BOOL extended = r.at[0] == 1 && r.at[1] == 0 && r.at[2] == 0xFF &&
                        r.at[3] == 0xFF;
  DWORD style;
  WORD count;
  RECT frame;
  if (extended) {
    r.at += 4;
    (void)read_dword(&r);  // helpID
    (void)read_dword(&r);  // exStyle
    style = read_dword(&r);
  } else {
    style = read_dword(&r);
    (void)read_dword(&r);  // exStyle
  }
  count = read_word(&r);
  frame.left = (SHORT)read_word(&r);
  frame.top = (SHORT)read_word(&r);
  frame.right = frame.left + (SHORT)read_word(&r);
  frame.bottom = frame.top + (SHORT)read_word(&r);
  WORD menuOrdinal;
  char menuName[UI_CLASS_NAME_BYTES + 1];
  read_text(&r, menuName, sizeof(menuName), &menuOrdinal);
  read_text(&r, NULL, 0, NULL);  // class: always the dialog class
  char title[UI_TITLE_BYTES + 1];
  read_text(&r, title, sizeof(title), NULL);
  if (style & DS_SETFONT) {
    (void)read_word(&r);  // point size
    if (extended) {
      (void)read_word(&r);  // weight
      (void)read_word(&r);  // italic, charset
    }
    read_text(&r, NULL, 0, NULL);  // face name; only the system font exists
  }
  if (menuOrdinal || menuName[0]) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
  }

  map_rect(&frame);
  const BOOL child = (style & WS_CHILD) != 0;
  DWORD windowStyle;
  if (child) {
    windowStyle = WS_CHILD | (style & (WS_BORDER | WS_CAPTION)) |
                  (style & WS_CHILD_EXTRA & ~(DWORD)WS_DISABLED);
  } else if ((style & WS_CAPTION) == WS_CAPTION) {
    windowStyle = WS_CAPTION;
  } else {
    windowStyle = (style & (WS_BORDER | WS_DLGFRAME | DS_MODALFRAME))
                      ? WS_BORDER : 0;
  }
  LONG x = frame.left, y = frame.top;
  const LONG width = frame.right - frame.left;
  const LONG height = frame.bottom - frame.top;
  if (owner && !child) {
    RECT ownerRect;
    if (GetWindowRect(owner, &ownerRect)) {
      if (style & DS_CENTER) {
        x = ownerRect.left + (ownerRect.right - ownerRect.left - width) / 2;
        y = ownerRect.top + (ownerRect.bottom - ownerRect.top - height) / 2;
      } else {
        x += ownerRect.left;
        y += ownerRect.top;
      }
    }
  }
  HWND dialog = CreateWindow(WC_DIALOG, title, windowStyle, x, y,
                             width > 0 ? width : 1, height > 0 ? height : 1,
                             child ? owner : NULL, NULL, NULL, NULL);
  if (!dialog) return NULL;
  // The template gives the client size; grow the window by its frame.
  RECT outer, inner;
  if (GetWindowRect(dialog, &outer) && GetClientRect(dialog, &inner)) {
    const LONG dw = (outer.right - outer.left) - (inner.right - inner.left);
    const LONG dh = (outer.bottom - outer.top) - (inner.bottom - inner.top);
    if (dw > 0 || dh > 0)
      (void)MoveWindow(dialog, x, y, width + dw, height + dh, FALSE);
  }
  memset(record, 0, sizeof(*record));
  record->used = TRUE;
  record->handle = hwnd_value(dialog);
  record->procedure = procedure;
  record->modal = modal;
  record->defaultId = IDOK;

  char text[UI_DIALOG_TEXT_BYTES];
  for (WORD i = 0; i < count; ++i) {
    align_dword(&r);
    DWORD itemStyle;
    UINT id;
    RECT rect;
    if (extended) {
      (void)read_dword(&r);  // helpID
      (void)read_dword(&r);  // exStyle
      itemStyle = read_dword(&r);
    } else {
      itemStyle = read_dword(&r);
      (void)read_dword(&r);  // exStyle
    }
    rect.left = (SHORT)read_word(&r);
    rect.top = (SHORT)read_word(&r);
    rect.right = rect.left + (SHORT)read_word(&r);
    rect.bottom = rect.top + (SHORT)read_word(&r);
    id = extended ? (UINT)read_dword(&r) : read_word(&r);
    WORD atom;
    char className[UI_CLASS_NAME_BYTES + 1];
    read_text(&r, className, sizeof(className), &atom);
    read_text(&r, text, sizeof(text), NULL);
    const WORD extra = read_word(&r);
    r.at += extra;
    const char *resolved = atom ? atom_class(atom) : className;
    BOOL created = FALSE;
    if (resolved) {
      if ((itemStyle & BS_TYPEMASK) == BS_DEFPUSHBUTTON &&
          !strcmp(ui_controls_class_name(resolved), WC_BUTTON))
        record->defaultId = (int)id;
      created = create_item(dialog, resolved, text, itemStyle, &rect, id);
    } else {
      local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    }
    if (!created) {
      const DWORD error = GetLastError();
      (void)DestroyWindow(dialog);
      SetLastError(error);
      return NULL;
    }
  }

  const uint32_t handle = hwnd_value(dialog);
  HWND focus = first_tab_item(dialog);
  const LRESULT initial = SendMessage(dialog, WM_INITDIALOG, (WPARAM)focus,
                                      param);
  if (!IsWindow(dialog) || !dialog_record(dialog) ||
      hwnd_value(dialog) != handle)
    return NULL;
  if (initial && focus) {
    if (SendMessage(focus, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL)
      (void)SendMessage(focus, EM_SETSEL, 0, -1);
    (void)SetFocus(focus);
  }
  if ((style & WS_VISIBLE) && !dialog_record(dialog)->ended)
    (void)ShowWindow(dialog, SW_SHOW);
  return dialog;
}

HWND CreateDialogIndirectParam(HINSTANCE instance, LPCDLGTEMPLATE dialog,
                               HWND owner, DLGPROC procedure, LPARAM param) {
  (void)instance;
  return create_dialog(dialog, owner, procedure, param, FALSE);
}

static LPCDLGTEMPLATE find_template(HINSTANCE instance, LPCSTR name) {
  HRSRC resource = FindResource(instance, name, RT_DIALOG);
  if (!resource) return NULL;
  return (LPCDLGTEMPLATE)LockResource(LoadResource(instance, resource));
}

HWND CreateDialogParam(HINSTANCE instance, LPCSTR name, HWND owner,
                       DLGPROC procedure, LPARAM param) {
  LPCDLGTEMPLATE dialog = find_template(instance, name);
  if (!dialog) return NULL;
  return create_dialog(dialog, owner, procedure, param, FALSE);
}

INT_PTR DialogBoxIndirectParam(HINSTANCE instance, LPCDLGTEMPLATE dialogTemplate,
                               HWND owner, DLGPROC procedure, LPARAM param) {
  (void)instance;
  HWND dialog = create_dialog(dialogTemplate, owner, procedure, param, TRUE);
  if (!dialog) return -1;
  const BOOL disableOwner = owner && IsWindowEnabled(owner);
  if (disableOwner) (void)EnableWindow(owner, FALSE);
  DialogRecord *record = dialog_record(dialog);
  if (record && !record->ended) (void)ShowWindow(dialog, SW_SHOW);
  INT_PTR result = -1;
  while ((record = dialog_record(dialog)) && !record->ended) {
    MSG message;
    const int got = GetMessage(&message, NULL, 0, 0);
    if (got == 0) {
      PostQuitMessage((int)message.wParam);
      break;
    }
    if (got < 0) break;
    if (!IsDialogMessage(dialog, &message)) {
      (void)TranslateMessage(&message);
      (void)DispatchMessage(&message);
    }
  }
  record = dialog_record(dialog);
  if (record && record->ended) result = record->result;
  if (disableOwner && IsWindow(owner)) (void)EnableWindow(owner, TRUE);
  if (IsWindow(dialog)) (void)DestroyWindow(dialog);
  return result;
}

INT_PTR DialogBoxParam(HINSTANCE instance, LPCSTR name, HWND owner,
                       DLGPROC procedure, LPARAM param) {
  LPCDLGTEMPLATE dialog = find_template(instance, name);
  if (!dialog) return -1;
  return DialogBoxIndirectParam(instance, dialog, owner, procedure, param);
}

BOOL EndDialog(HWND dialog, INT_PTR result) {
  DialogRecord *record = dialog_record(dialog);
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  record->ended = TRUE;
  record->result = result;
  (void)ShowWindow(dialog, SW_HIDE);
  return TRUE;
}

/* Navigation. */

static BOOL tabbable(HWND item) {
  const DWORD style = (DWORD)GetWindowLong(item, GWL_STYLE);
  return (style & WS_VISIBLE) && !(style & WS_DISABLED);
}

static HWND first_tab_item(HWND dialog) {
  for (HWND item = GetWindow(dialog, GW_CHILD); item;
       item = GetWindow(item, GW_HWNDNEXT))
    if ((ui_control_style(item) & WS_TABSTOP) && !(ui_control_style(item) &
                                                   WS_DISABLED))
      return item;
  return NULL;
}

static HWND step(HWND dialog, HWND item, BOOL previous) {
  HWND next = item ? GetWindow(item, previous ? GW_HWNDPREV : GW_HWNDNEXT)
                   : NULL;
  if (!next) {
    HWND first = GetWindow(dialog, GW_CHILD);
    if (!first) return NULL;
    next = previous ? GetWindow(first, GW_HWNDLAST) : first;
  }
  return next;
}

static int child_count(HWND dialog) {
  int count = 0;
  for (HWND item = GetWindow(dialog, GW_CHILD); item;
       item = GetWindow(item, GW_HWNDNEXT))
    ++count;
  return count;
}

HWND GetNextDlgTabItem(HWND dialog, HWND control, BOOL previous) {
  if (!IsWindow(dialog)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  if (control && GetParent(control) != dialog) control = NULL;
  const int count = child_count(dialog);
  HWND item = control;
  for (int n = 0; n < count; ++n) {
    item = step(dialog, item, previous);
    if (!item) break;
    if ((ui_control_style(item) & WS_TABSTOP) && tabbable(item)) return item;
  }
  return control;
}

HWND GetNextDlgGroupItem(HWND dialog, HWND control, BOOL previous) {
  if (!IsWindow(dialog)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  if (!control || GetParent(control) != dialog)
    return GetNextDlgTabItem(dialog, NULL, previous);
  HWND start = control;
  while (!(ui_control_style(start) & WS_GROUP)) {
    HWND before = GetWindow(start, GW_HWNDPREV);
    if (!before) break;
    start = before;
  }
  HWND end = start;
  for (HWND after = GetWindow(start, GW_HWNDNEXT);
       after && !(ui_control_style(after) & WS_GROUP);
       after = GetWindow(after, GW_HWNDNEXT))
    end = after;
  HWND item = control;
  for (;;) {
    if (previous) item = item == start ? end : GetWindow(item, GW_HWNDPREV);
    else item = item == end ? start : GetWindow(item, GW_HWNDNEXT);
    if (!item || item == control) return control;
    if (tabbable(item)) return item;
  }
}

static void focus_item(HWND item) {
  if (!item) return;
  if (SendMessage(item, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL)
    (void)SendMessage(item, EM_SETSEL, 0, -1);
  (void)SetFocus(item);
}

static BOOL in_dialog(HWND dialog, HWND window) {
  return window && (window == dialog || IsChild(dialog, window));
}

static void press(HWND dialog, int id) {
  HWND item = GetDlgItem(dialog, id);
  if (item && !IsWindowEnabled(item)) return;
  (void)SendMessage(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
                    (LPARAM)item);
}

BOOL IsDialogMessage(HWND dialog, MSG *message) {
  if (!message || !IsWindow(dialog) || !in_dialog(dialog, message->hwnd))
    return FALSE;
  const UINT kind = message->message;
  const BOOL keyboard = kind == WM_KEYDOWN || kind == WM_KEYUP ||
                        kind == WM_CHAR || kind == WM_SYSKEYDOWN ||
                        kind == WM_SYSKEYUP || kind == WM_SYSCHAR;
  HWND target = message->hwnd;
  HWND focus = GetFocus();
  if (keyboard && in_dialog(dialog, focus)) target = focus;
  if (kind == WM_KEYDOWN || kind == WM_CHAR) {
    const LRESULT code = target != dialog
        ? SendMessage(target, WM_GETDLGCODE, message->wParam, (LPARAM)message)
        : 0;
    if (code & DLGC_WANTALLKEYS) goto dispatch;
    if (kind == WM_CHAR) {
      if ((message->wParam == '\t' || message->wParam == '\r' ||
           message->wParam == 0x1B) && !(code & DLGC_WANTCHARS))
        return TRUE;
      goto dispatch;
    }
    const BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (message->wParam) {
      case VK_TAB:
        if (code & DLGC_WANTTAB) break;
        focus_item(GetNextDlgTabItem(dialog, target == dialog ? NULL : target,
                                     shift));
        return TRUE;
      case VK_RETURN: {
        int id;
        if (code & DLGC_DEFPUSHBUTTON || code & DLGC_UNDEFPUSHBUTTON)
          id = GetDlgCtrlID(target);
        else
          id = LOWORD(SendMessage(dialog, DM_GETDEFID, 0, 0));
        press(dialog, id);
        return TRUE;
      }
      case VK_ESCAPE: press(dialog, IDCANCEL); return TRUE;
      case VK_UP:
      case VK_DOWN:
      case VK_LEFT:
      case VK_RIGHT: {
        if (code & DLGC_WANTARROWS) break;
        HWND next = GetNextDlgGroupItem(
            dialog, target == dialog ? NULL : target,
            message->wParam == VK_UP || message->wParam == VK_LEFT);
        if (next && next != target) {
          focus_item(next);
          if ((SendMessage(next, WM_GETDLGCODE, 0, 0) & DLGC_RADIOBUTTON) &&
              (ui_control_style(next) & BS_TYPEMASK) == BS_AUTORADIOBUTTON)
            (void)SendMessage(next, BM_CLICK, 0, 0);
        }
        return TRUE;
      }
      default: break;
    }
  }
dispatch: {
    MSG copy = *message;
    copy.hwnd = target;
    (void)DispatchMessage(&copy);
  }
  return TRUE;
}

LRESULT DefDlgProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
  DialogRecord *record = dialog_record(dialog);
  switch (message) {
    case WM_CLOSE:
      press(dialog, IDCANCEL);
      return 0;
    case DM_GETDEFID:
      return record ? MAKELONG(record->defaultId, DC_HASDEFID) : 0;
    case DM_SETDEFID:
      if (record) record->defaultId = (int)wParam;
      return TRUE;
    case WM_NEXTDLGCTL:
      if (LOWORD(lParam)) focus_item((HWND)wParam);
      else focus_item(GetNextDlgTabItem(dialog, GetFocus(), wParam != 0));
      return 0;
    case WM_SETFOCUS: {
      HWND last = record ? hwnd_from_value(record->lastFocus) : NULL;
      if (!last || !IsWindow(last) || !IsChild(dialog, last))
        last = first_tab_item(dialog);
      if (last) focus_item(last);
      return 0;
    }
    case WM_KILLFOCUS:
      if (record && wParam && IsChild(dialog, (HWND)wParam)) return 0;
      if (record && ui_focus_window && IsChild(dialog,
                                               hwnd_from_value(ui_focus_window)))
        record->lastFocus = ui_focus_window;
      return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR: {
      HWND focus = GetFocus();
      if (focus && focus != dialog && IsChild(dialog, focus))
        return SendMessage(focus, message, wParam, lParam);
      return 0;
    }
    case WM_GETDLGCODE: return 0;
    default: return DefWindowProc(dialog, message, wParam, lParam);
  }
}

LRESULT CALLBACK ui_dialog_proc(HWND dialog, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  DialogRecord *record = dialog_record(dialog);
  if (record && record->procedure) {
    const uint32_t handle = record->handle;
    const INT_PTR handled = record->procedure(dialog, message, wParam, lParam);
    record = dialog_record(dialog);
    if (!record || record->handle != handle) return handled;
    if (handled) {
      if (message == WM_INITDIALOG) return handled;
      return record->msgResult;
    }
  }
  return DefDlgProc(dialog, message, wParam, lParam);
}

/* Dialog item helpers. */

HWND GetDlgItem(HWND dialog, int id) {
  if (!IsWindow(dialog)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  for (HWND item = GetWindow(dialog, GW_CHILD); item;
       item = GetWindow(item, GW_HWNDNEXT))
    if (GetDlgCtrlID(item) == id) return item;
  local_failure(UI_STATUS_NOT_FOUND, ERROR_CONTROL_ID_NOT_FOUND);
  return NULL;
}

LRESULT SendDlgItemMessage(HWND dialog, int id, UINT message, WPARAM wParam,
                           LPARAM lParam) {
  HWND item = GetDlgItem(dialog, id);
  return item ? SendMessage(item, message, wParam, lParam) : 0;
}

UINT GetDlgItemText(HWND dialog, int id, LPSTR buffer, int size) {
  if (buffer && size > 0) buffer[0] = '\0';
  HWND item = GetDlgItem(dialog, id);
  if (!item) return 0;
  return (UINT)GetWindowText(item, buffer, size);
}

BOOL SetDlgItemText(HWND dialog, int id, LPCSTR text) {
  HWND item = GetDlgItem(dialog, id);
  if (!item) return FALSE;
  return SendMessage(item, WM_SETTEXT, 0, (LPARAM)(text ? text : "")) != 0;
}

UINT GetDlgItemInt(HWND dialog, int id, BOOL *translated, BOOL isSigned) {
  char text[32];
  if (translated) *translated = FALSE;
  if (!GetDlgItemText(dialog, id, text, (int)sizeof(text)) && !text[0])
    return 0;
  const char *p = text;
  while (*p == ' ') ++p;
  BOOL negative = FALSE;
  if (*p == '-' && isSigned) {
    negative = TRUE;
    ++p;
  }
  if (*p < '0' || *p > '9') return 0;
  uint64_t value = 0;
  for (; *p >= '0' && *p <= '9'; ++p) {
    value = value * 10u + (uint64_t)(*p - '0');
    if (value > (negative ? 0x80000000ull
                          : (isSigned ? 0x7FFFFFFFull : 0xFFFFFFFFull)))
      return 0;
  }
  while (*p == ' ') ++p;
  if (*p) return 0;
  if (translated) *translated = TRUE;
  return negative ? (UINT)(-(int64_t)value) : (UINT)value;
}

BOOL SetDlgItemInt(HWND dialog, int id, UINT value, BOOL isSigned) {
  char text[16];
  char *p = text + sizeof(text) - 1;
  *p = '\0';
  const BOOL negative = isSigned && (int)value < 0;
  uint32_t magnitude = negative ? (uint32_t)(-(int64_t)(int)value) : value;
  do {
    *--p = (char)('0' + magnitude % 10u);
    magnitude /= 10u;
  } while (magnitude);
  if (negative) *--p = '-';
  return SetDlgItemText(dialog, id, p);
}

BOOL CheckDlgButton(HWND dialog, int id, UINT check) {
  HWND item = GetDlgItem(dialog, id);
  if (!item) return FALSE;
  (void)SendMessage(item, BM_SETCHECK, check, 0);
  return TRUE;
}

UINT IsDlgButtonChecked(HWND dialog, int id) {
  HWND item = GetDlgItem(dialog, id);
  return item ? (UINT)SendMessage(item, BM_GETCHECK, 0, 0) : 0;
}

BOOL CheckRadioButton(HWND dialog, int first, int last, int check) {
  if (!IsWindow(dialog)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  for (HWND item = GetWindow(dialog, GW_CHILD); item;
       item = GetWindow(item, GW_HWNDNEXT)) {
    const int id = GetDlgCtrlID(item);
    if (id >= first && id <= last)
      (void)SendMessage(item, BM_SETCHECK, id == check ? BST_CHECKED
                                                       : BST_UNCHECKED, 0);
  }
  return TRUE;
}
