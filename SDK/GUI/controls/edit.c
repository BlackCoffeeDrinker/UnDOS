#include <windows.h>
#include <undoscrt/string.h>

#include "../private/controls.h"

/* EDIT: single-line text entry. Positions are byte offsets. */

#define EDIT_MARGIN 2

static UINT sel_start(const ControlRecord *c) {
  return c->caret < c->anchor ? c->caret : c->anchor;
}

static UINT sel_end(const ControlRecord *c) {
  return c->caret < c->anchor ? c->anchor : c->caret;
}

static void changed(HWND window, ControlRecord *control) {
  control->modified = TRUE;
  InvalidateRect(window, NULL, TRUE);
  ui_control_notify(window, EN_UPDATE);
  ui_control_notify(window, EN_CHANGE);
}

static char convert(DWORD style, char c) {
  if ((style & ES_UPPERCASE) && c >= 'a' && c <= 'z') return (char)(c - 32);
  if ((style & ES_LOWERCASE) && c >= 'A' && c <= 'Z') return (char)(c + 32);
  return c;
}

/* Replaces the selection; returns FALSE when the limit truncated text. */
static BOOL replace(HWND window, ControlRecord *control, const char *text) {
  const DWORD style = ui_control_style(window);
  const UINT start = sel_start(control), end = sel_end(control);
  UINT length = (UINT)strlen(control->text);
  char *buffer = control->text;
  memmove(buffer + start, buffer + end, length - end + 1);
  length -= end - start;
  size_t insert = text ? strlen(text) : 0;
  BOOL complete = TRUE;
  if (length + insert > control->limit) {
    insert = control->limit > length ? control->limit - length : 0;
    complete = FALSE;
  }
  memmove(buffer + start + insert, buffer + start, length - start + 1);
  for (size_t i = 0; i < insert; ++i)
    buffer[start + i] = convert(style, text[i]);
  control->caret = control->anchor = start + (UINT)insert;
  if (!complete) ui_control_notify(window, EN_MAXTEXT);
  return complete;
}

static void move_caret(HWND window, ControlRecord *control, UINT position,
                       BOOL extend) {
  const UINT length = (UINT)strlen(control->text);
  if (position > length) position = length;
  control->caret = position;
  if (!extend) control->anchor = position;
  InvalidateRect(window, NULL, TRUE);
}

static void display_text(HWND window, const ControlRecord *control,
                         char *out) {
  const BOOL password = (ui_control_style(window) & ES_PASSWORD) != 0;
  size_t i = 0;
  for (; control->text[i]; ++i) out[i] = password ? '*' : control->text[i];
  out[i] = '\0';
}

static LONG width_of(HDC dc, const char *text, UINT count) {
  SIZE size = {0, 0};
  if (count) GetTextExtentPoint32(dc, text, (int)count, &size);
  return size.cx;
}

static void paint(HWND window, ControlRecord *control) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(window, &ps);
  if (!dc) return;
  RECT client;
  GetClientRect(window, &client);
  const DWORD style = ui_control_style(window);
  const BOOL enabled = IsWindowEnabled(window);
  ui_control_fill(dc, &client, enabled && !(style & ES_READONLY)
                                   ? COLOR_WINDOW : COLOR_BTNFACE);
  if (style & WS_BORDER) ui_control_frame(dc, &client, COLOR_WINDOWFRAME);
  HFONT previous = ui_control_select_font(dc, control);
  char shown[UI_CONTROL_TEXT_BYTES];
  display_text(window, control, shown);
  control->charWidth = width_of(dc, "M", 1);
  const LONG room = client.right - client.left - 2 * EDIT_MARGIN;
  if (control->caret < control->scroll) control->scroll = control->caret;
  while (control->scroll < control->caret &&
         width_of(dc, shown + control->scroll,
                  control->caret - control->scroll) > room)
    ++control->scroll;
  const char *visible = shown + control->scroll;
  const UINT visibleLength = (UINT)strlen(visible);
  LONG x = client.left + EDIT_MARGIN;
  const DWORD align = style & (ES_CENTER | ES_RIGHT);
  if (align) {
    const LONG used = width_of(dc, visible, visibleLength);
    if (used < room)
      x += (align & ES_RIGHT) ? room - used : (room - used) / 2;
  }
  const LONG lineHeight = ui_control_line_height();
  const LONG y = (client.top + client.bottom - lineHeight) / 2;
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
  TextOut(dc, x, y, visible, (int)visibleLength);
  const BOOL focused = GetFocus() == window;
  UINT start = sel_start(control), end = sel_end(control);
  if (focused && start != end) {
    if (start < control->scroll) start = control->scroll;
    if (end > start) {
      RECT highlight;
      const LONG left = x + width_of(dc, visible, start - control->scroll);
      SetRect(&highlight, left, y,
              left + width_of(dc, shown + start, end - start), y + lineHeight);
      ui_control_fill(dc, &highlight, COLOR_HIGHLIGHT);
      SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
      TextOut(dc, left, y, shown + start, (int)(end - start));
    }
  }
  if (focused) {
    const LONG caretX =
        x + width_of(dc, visible, control->caret - control->scroll);
    RECT caret;
    SetRect(&caret, caretX, y, caretX + 1, y + lineHeight);
    ui_control_fill(dc, &caret, COLOR_WINDOWTEXT);
  }
  if (previous) SelectObject(dc, previous);
  EndPaint(window, &ps);
}

static UINT hit(const ControlRecord *control, LONG x) {
  const LONG cell = control->charWidth > 0 ? control->charWidth : 8;
  LONG column = (x - EDIT_MARGIN + cell / 2) / cell;
  if (column < 0) column = 0;
  return control->scroll + (UINT)column;
}

static LRESULT key_down(HWND window, ControlRecord *control, WPARAM key) {
  const BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  const UINT length = (UINT)strlen(control->text);
  switch (key) {
    case VK_LEFT:
      if (!shift && control->caret != control->anchor)
        move_caret(window, control, sel_start(control), FALSE);
      else if (control->caret)
        move_caret(window, control, control->caret - 1, shift);
      return 0;
    case VK_RIGHT:
      if (!shift && control->caret != control->anchor)
        move_caret(window, control, sel_end(control), FALSE);
      else
        move_caret(window, control, control->caret + 1, shift);
      return 0;
    case VK_HOME: move_caret(window, control, 0, shift); return 0;
    case VK_END: move_caret(window, control, length, shift); return 0;
    case VK_DELETE:
      if (ui_control_style(window) & ES_READONLY) return 0;
      if (control->caret == control->anchor) {
        if (control->caret >= length) return 0;
        control->anchor = control->caret + 1;
      }
      replace(window, control, "");
      changed(window, control);
      return 0;
    default: return 0;
  }
}

static LRESULT character(HWND window, ControlRecord *control, WPARAM code) {
  if (ui_control_style(window) & ES_READONLY) return 0;
  if (code == VK_BACK) {
    if (control->caret == control->anchor) {
      if (!control->caret) return 0;
      control->anchor = control->caret - 1;
    }
    replace(window, control, "");
    changed(window, control);
    return 0;
  }
  if (code < 0x20 || code > 0xFF) return 0;
  const char text[2] = {(char)code, '\0'};
  if (control->caret == control->anchor &&
      strlen(control->text) >= control->limit) {
    ui_control_notify(window, EN_MAXTEXT);
    return 0;
  }
  replace(window, control, text);
  changed(window, control);
  return 0;
}

static void set_selection(HWND window, ControlRecord *control, LONG start,
                          LONG end) {
  const LONG length = (LONG)strlen(control->text);
  if (start < 0) {
    control->anchor = control->caret;
  } else {
    if (end < 0 || end > length) end = length;
    if (start > length) start = length;
    control->anchor = (UINT)start;
    control->caret = (UINT)end;
  }
  InvalidateRect(window, NULL, TRUE);
}

LRESULT CALLBACK ui_edit_proc(HWND window, UINT message, WPARAM wParam,
                              LPARAM lParam) {
  ControlRecord *control = ui_control(window);
  if (!control) return DefWindowProc(window, message, wParam, lParam);
  switch (message) {
    case WM_PAINT: paint(window, control); return 0;
    case WM_LBUTTONDOWN:
      if (!IsWindowEnabled(window)) return 0;
      SetFocus(window);
      move_caret(window, control, hit(control, (SHORT)LOWORD(lParam)),
                 (GetKeyState(VK_SHIFT) & 0x8000) != 0);
      return 0;
    case WM_LBUTTONDBLCLK:
      set_selection(window, control, 0, -1);
      return 0;
    case WM_KEYDOWN: return key_down(window, control, wParam);
    case WM_CHAR: return character(window, control, wParam);
    case WM_SETFOCUS:
      InvalidateRect(window, NULL, TRUE);
      ui_control_notify(window, EN_SETFOCUS);
      return 0;
    case WM_KILLFOCUS:
      InvalidateRect(window, NULL, TRUE);
      ui_control_notify(window, EN_KILLFOCUS);
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_HASSETSEL;
    case WM_SETTEXT: {
      control->caret = 0;
      control->anchor = (UINT)strlen(control->text);
      replace(window, control, (const char *)lParam);
      control->caret = control->anchor = 0;
      control->scroll = 0;
      control->modified = FALSE;
      InvalidateRect(window, NULL, TRUE);
      ui_control_notify(window, EN_CHANGE);
      return TRUE;
    }
    case EM_GETSEL: {
      const DWORD start = sel_start(control), end = sel_end(control);
      if (wParam) *(DWORD *)wParam = start;
      if (lParam) *(DWORD *)lParam = end;
      return (LRESULT)(DWORD)MAKELONG(start, end);
    }
    case EM_SETSEL:
      set_selection(window, control, (LONG)wParam, (LONG)lParam);
      return 0;
    case EM_REPLACESEL:
      (void)replace(window, control, (const char *)lParam);
      changed(window, control);
      return 0;
    case EM_LIMITTEXT:
      control->limit = (!wParam || wParam > UI_CONTROL_TEXT_BYTES - 1)
                           ? UI_CONTROL_TEXT_BYTES - 1 : (UINT)wParam;
      return 0;
    case EM_GETMODIFY: return control->modified;
    case EM_SETMODIFY: control->modified = wParam != 0; return 0;
    case EM_SETREADONLY: {
      const DWORD style = ui_control_style(window);
      SetWindowLong(window, GWL_STYLE,
                    (LONG)(wParam ? style | ES_READONLY : style & ~ES_READONLY));
      InvalidateRect(window, NULL, TRUE);
      return TRUE;
    }
    case WM_GETTEXT:
    case WM_GETTEXTLENGTH:
    case WM_SETFONT:
    case WM_GETFONT:
    case WM_ENABLE:
      return ui_control_text_message(control, message, wParam, lParam);
    default: return DefWindowProc(window, message, wParam, lParam);
  }
}
