#include <windows.h>
#include <undoscrt/heap.h>
#include <undoscrt/string.h>

#include "../private/controls.h"

/* LISTBOX: single, multiple and extended selection, fixed or variable
   owner-draw items, and an internal vertical scroll bar (WS_VSCROLL). */

#define TEXT_MARGIN 2

static BOOL owner_draw(DWORD style) {
  return (style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) != 0;
}

static BOOL has_strings(DWORD style) {
  return !owner_draw(style) || (style & LBS_HASSTRINGS);
}

static BOOL multiple(DWORD style) {
  return (style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0;
}

void ui_listbox_release(ControlRecord *control) {
  for (int i = 0; i < control->count; ++i) crt_free(control->items[i].text);
  crt_free(control->items);
  control->items = NULL;
  control->count = control->capacity = 0;
}

static char fold(char c) {
  return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static int compare(const char *a, const char *b) {
  for (; *a && fold(*a) == fold(*b); ++a, ++b) {}
  return (unsigned char)fold(*a) - (unsigned char)fold(*b);
}

static BOOL prefix(const char *text, const char *start) {
  for (; *start; ++text, ++start)
    if (fold(*text) != fold(*start)) return FALSE;
  return TRUE;
}

/* Client area without the internal scroll bar. */
static void list_rect(HWND window, RECT *rect, RECT *bar) {
  GetClientRect(window, rect);
  if (bar) *bar = *rect;
  if (ui_control_style(window) & WS_VSCROLL) {
    rect->right -= UI_SCROLL_ARROW;
    if (bar) bar->left = rect->right;
  }
}

static UINT item_height(const ControlRecord *control, int index) {
  if (index >= 0 && index < control->count && control->items[index].height)
    return control->items[index].height;
  return control->itemHeight ? control->itemHeight : 16;
}

static int visible_count(HWND window, const ControlRecord *control) {
  RECT rect;
  list_rect(window, &rect, NULL);
  LONG y = rect.top;
  int shown = 0;
  for (int i = control->top; i < control->count; ++i) {
    y += (LONG)item_height(control, i);
    if (y > rect.bottom) break;
    ++shown;
  }
  if (!shown) {
    const LONG h = (LONG)item_height(control, -1);
    shown = h > 0 ? (int)((rect.bottom - rect.top) / h) : 1;
    if (shown < 1) shown = 1;
  }
  return shown;
}

static void sync_bar(HWND window, ControlRecord *control) {
  control->bar.minimum = 0;
  control->bar.maximum = control->count > 0 ? control->count - 1 : 0;
  control->bar.page = (UINT)visible_count(window, control);
  control->bar.position = control->top;
}

static void set_top(HWND window, ControlRecord *control, int top) {
  const int page = visible_count(window, control);
  if (top > control->count - page) top = control->count - page;
  if (top < 0) top = 0;
  if (top == control->top) return;
  control->top = top;
  sync_bar(window, control);
  InvalidateRect(window, NULL, TRUE);
}

static void ensure_visible(HWND window, ControlRecord *control, int index) {
  if (index < 0) return;
  if (index < control->top) {
    set_top(window, control, index);
    return;
  }
  const int page = visible_count(window, control);
  if (index >= control->top + page) set_top(window, control, index - page + 1);
}

static BOOL item_rect(HWND window, const ControlRecord *control, int index,
                      RECT *out) {
  if (index < 0 || index >= control->count) return FALSE;
  RECT rect;
  list_rect(window, &rect, NULL);
  LONG y = rect.top;
  if (index >= control->top)
    for (int i = control->top; i < index; ++i) y += (LONG)item_height(control, i);
  else
    for (int i = index; i < control->top; ++i) y -= (LONG)item_height(control, i);
  SetRect(out, rect.left, y, rect.right, y + (LONG)item_height(control, index));
  return TRUE;
}

static int item_at(HWND window, const ControlRecord *control, LONG y) {
  RECT rect;
  list_rect(window, &rect, NULL);
  LONG top = rect.top;
  for (int i = control->top; i < control->count; ++i) {
    const LONG bottom = top + (LONG)item_height(control, i);
    if (y >= top && y < bottom) return i;
    top = bottom;
    if (top >= rect.bottom) break;
  }
  return -1;
}

static BOOL selected(HWND window, const ControlRecord *control, int index) {
  if (index < 0 || index >= control->count) return FALSE;
  if (multiple(ui_control_style(window))) return control->items[index].selected;
  return index == control->current;
}

static void draw_item(HWND window, HDC dc, ControlRecord *control, int index,
                      const RECT *rect, BOOL focused) {
  const DWORD style = ui_control_style(window);
  const BOOL isSelected = selected(window, control, index);
  const BOOL hasFocus = focused && index == control->caretIndex;
  if (owner_draw(style)) {
    DRAWITEMSTRUCT item = {0};
    item.CtlType = ODT_LISTBOX;
    item.CtlID = (UINT)GetDlgCtrlID(window);
    item.itemID = (UINT)index;
    item.itemAction = ODA_DRAWENTIRE;
    if (isSelected) item.itemState |= ODS_SELECTED;
    if (hasFocus) item.itemState |= ODS_FOCUS;
    if (!IsWindowEnabled(window)) item.itemState |= ODS_DISABLED;
    item.hwndItem = window;
    item.hDC = dc;
    item.rcItem = *rect;
    item.itemData = control->items[index].data;
    (void)SendMessage(GetParent(window), WM_DRAWITEM, item.CtlID,
                      (LPARAM)&item);
    return;
  }
  ui_control_fill(dc, rect, isSelected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
  SetTextColor(dc, GetSysColor(isSelected ? COLOR_HIGHLIGHTTEXT
                                          : COLOR_WINDOWTEXT));
  RECT text = *rect;
  text.left += TEXT_MARGIN;
  UINT format = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
  if (style & LBS_USETABSTOPS) format |= DT_EXPANDTABS;
  DrawText(dc, control->items[index].text ? control->items[index].text : "",
           -1, &text, format);
  if (hasFocus) DrawFocusRect(dc, rect);
}

static void paint(HWND window, ControlRecord *control) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(window, &ps);
  if (!dc) return;
  RECT rect, bar;
  list_rect(window, &rect, &bar);
  const DWORD style = ui_control_style(window);
  ui_control_fill(dc, &rect, COLOR_WINDOW);
  HFONT previous = ui_control_select_font(dc, control);
  SetBkMode(dc, TRANSPARENT);
  const BOOL focused = GetFocus() == window;
  LONG y = rect.top;
  for (int i = control->top; i < control->count && y < rect.bottom; ++i) {
    RECT item;
    SetRect(&item, rect.left, y, rect.right, y + (LONG)item_height(control, i));
    draw_item(window, dc, control, i, &item, focused);
    y = item.bottom;
  }
  if (previous) SelectObject(dc, previous);
  if (style & WS_VSCROLL) {
    sync_bar(window, control);
    ui_scroll_draw(dc, &bar, TRUE, &control->bar,
                   control->count > (int)control->bar.page);
  }
  if (style & WS_BORDER) {
    RECT client;
    GetClientRect(window, &client);
    ui_control_frame(dc, &client, COLOR_WINDOWFRAME);
  }
  EndPaint(window, &ps);
}

static void notify(HWND window, UINT code) {
  if (ui_control_style(window) & LBS_NOTIFY) ui_control_notify(window, code);
}

static LRESULT insert(HWND window, ControlRecord *control, int index,
                      LPARAM value, BOOL sorted) {
  const DWORD style = ui_control_style(window);
  const BOOL strings = has_strings(style);
  if (index < -1 || index > control->count) return LB_ERR;
  if (control->count == control->capacity) {
    const int capacity = control->capacity ? control->capacity * 2 : 16;
    ListItem *grown = (ListItem *)crt_realloc(
        control->items, (size_t)capacity * sizeof(ListItem));
    if (!grown) return LB_ERRSPACE;
    control->items = grown;
    control->capacity = capacity;
  }
  ListItem item = {0};
  if (strings) {
    const char *text = value ? (const char *)value : "";
    const size_t length = strlen(text);
    item.text = (char *)crt_malloc(length + 1);
    if (!item.text) return LB_ERRSPACE;
    memcpy(item.text, text, length + 1);
  } else {
    item.data = (ULONG_PTR)value;
  }
  if (index == -1) index = control->count;
  if (sorted && strings && (style & LBS_SORT)) {
    index = 0;
    while (index < control->count &&
           compare(control->items[index].text, item.text) <= 0)
      ++index;
  }
  memmove(&control->items[index + 1], &control->items[index],
          (size_t)(control->count - index) * sizeof(ListItem));
  control->items[index] = item;
  ++control->count;
  if (style & LBS_OWNERDRAWVARIABLE) {
    MEASUREITEMSTRUCT measure = {0};
    measure.CtlType = ODT_LISTBOX;
    measure.CtlID = (UINT)GetDlgCtrlID(window);
    measure.itemID = (UINT)index;
    measure.itemHeight = control->itemHeight;
    measure.itemData = strings ? (ULONG_PTR)item.text : item.data;
    (void)SendMessage(GetParent(window), WM_MEASUREITEM, measure.CtlID,
                      (LPARAM)&measure);
    control->items[index].height = measure.itemHeight;
  }
  if (control->current >= index) ++control->current;
  if (control->caretIndex >= index && control->count > 1) ++control->caretIndex;
  sync_bar(window, control);
  InvalidateRect(window, NULL, TRUE);
  return index;
}

static void delete_notify(HWND window, ControlRecord *control, int index) {
  if (!owner_draw(ui_control_style(window))) return;
  DELETEITEMSTRUCT item = {0};
  item.CtlType = ODT_LISTBOX;
  item.CtlID = (UINT)GetDlgCtrlID(window);
  item.itemID = (UINT)index;
  item.hwndItem = window;
  item.itemData = control->items[index].data;
  (void)SendMessage(GetParent(window), WM_DELETEITEM, item.CtlID,
                    (LPARAM)&item);
}

static LRESULT remove_item(HWND window, ControlRecord *control, int index) {
  if (index < 0 || index >= control->count) return LB_ERR;
  delete_notify(window, control, index);
  crt_free(control->items[index].text);
  memmove(&control->items[index], &control->items[index + 1],
          (size_t)(control->count - index - 1) * sizeof(ListItem));
  --control->count;
  if (control->current == index) control->current = -1;
  else if (control->current > index) --control->current;
  if (control->caretIndex > index ||
      (control->caretIndex == index && index == control->count))
    --control->caretIndex;
  if (control->caretIndex < 0) control->caretIndex = 0;
  set_top(window, control, control->top);
  sync_bar(window, control);
  InvalidateRect(window, NULL, TRUE);
  return control->count;
}

static void reset(HWND window, ControlRecord *control) {
  for (int i = 0; i < control->count; ++i) delete_notify(window, control, i);
  ui_listbox_release(control);
  control->current = -1;
  control->caretIndex = control->top = control->anchorIndex = 0;
  sync_bar(window, control);
  InvalidateRect(window, NULL, TRUE);
}

static int find(HWND window, const ControlRecord *control, int start,
                LPARAM value, BOOL exact) {
  if (!control->count) return LB_ERR;
  const BOOL strings = has_strings(ui_control_style(window));
  if (start < -1 || start >= control->count) start = -1;
  for (int n = 1; n <= control->count; ++n) {
    const int i = (start + n) % control->count;
    if (!strings) {
      if (control->items[i].data == (ULONG_PTR)value) return i;
      continue;
    }
    const char *text = value ? (const char *)value : "";
    if (exact ? !compare(control->items[i].text, text)
              : prefix(control->items[i].text, text))
      return i;
  }
  return LB_ERR;
}

static void select_range(ControlRecord *control, int from, int to, BOOL on) {
  if (from > to) {
    const int t = from;
    from = to;
    to = t;
  }
  for (int i = from; i <= to && i < control->count; ++i)
    if (i >= 0) control->items[i].selected = on;
}

/* Moves the caret as keyboard or mouse input does and updates selection. */
static void move_to(HWND window, ControlRecord *control, int index,
                    BOOL shift, BOOL ctrl, BOOL toggle) {
  if (!control->count) return;
  if (index < 0) index = 0;
  if (index >= control->count) index = control->count - 1;
  const DWORD style = ui_control_style(window);
  if (style & LBS_EXTENDEDSEL) {
    if (shift) {
      if (!ctrl) select_range(control, 0, control->count - 1, FALSE);
      select_range(control, control->anchorIndex, index, TRUE);
    } else if (ctrl) {
      if (toggle) control->items[index].selected = !control->items[index].selected;
      control->anchorIndex = index;
    } else {
      select_range(control, 0, control->count - 1, FALSE);
      control->items[index].selected = TRUE;
      control->anchorIndex = index;
    }
  } else if (style & LBS_MULTIPLESEL) {
    if (toggle) control->items[index].selected = !control->items[index].selected;
  } else {
    control->current = index;
  }
  control->caretIndex = index;
  ensure_visible(window, control, index);
  InvalidateRect(window, NULL, TRUE);
  notify(window, LBN_SELCHANGE);
}

static void scroll(HWND window, ControlRecord *control, int code, int thumb) {
  const int page = visible_count(window, control);
  switch (code) {
    case SB_LINEUP: set_top(window, control, control->top - 1); break;
    case SB_LINEDOWN: set_top(window, control, control->top + 1); break;
    case SB_PAGEUP: set_top(window, control, control->top - page); break;
    case SB_PAGEDOWN: set_top(window, control, control->top + page); break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK: set_top(window, control, thumb); break;
    case SB_TOP: set_top(window, control, 0); break;
    case SB_BOTTOM: set_top(window, control, control->count); break;
    default: break;
  }
}

static LRESULT key_down(HWND window, ControlRecord *control, WPARAM key) {
  const BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  const BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  const DWORD style = ui_control_style(window);
  const int page = visible_count(window, control);
  int caret = multiple(style) ? control->caretIndex : control->current;
  if (caret < 0) caret = 0;
  int target;
  switch (key) {
    case VK_UP: target = caret - 1; break;
    case VK_DOWN: target = control->current < 0 && !multiple(style) ? 0
                                                                   : caret + 1;
      break;
    case VK_PRIOR: target = caret - page + 1; break;
    case VK_NEXT: target = caret + page - 1; break;
    case VK_HOME: target = 0; break;
    case VK_END: target = control->count - 1; break;
    case VK_SPACE:
      if (multiple(style)) move_to(window, control, caret, shift, TRUE, TRUE);
      return 0;
    default: return 0;
  }
  move_to(window, control, target, shift, ctrl, FALSE);
  return 0;
}

static LRESULT character(HWND window, ControlRecord *control, WPARAM code) {
  if (code <= 0x20 || code > 0xFF || !has_strings(ui_control_style(window)))
    return 0;
  const char text[2] = {(char)code, '\0'};
  const int start = multiple(ui_control_style(window)) ? control->caretIndex
                                                        : control->current;
  const int found = find(window, control, start, (LPARAM)text, FALSE);
  if (found >= 0) move_to(window, control, found, FALSE, FALSE, FALSE);
  return 0;
}

static LRESULT button_down(HWND window, ControlRecord *control, LPARAM lParam,
                           BOOL doubleClick) {
  if (!IsWindowEnabled(window)) return 0;
  SetFocus(window);
  POINT point = {(SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam)};
  RECT rect, bar;
  list_rect(window, &rect, &bar);
  if ((ui_control_style(window) & WS_VSCROLL) && point.x >= bar.left) {
    sync_bar(window, control);
    int thumb = control->top;
    const int code = ui_scroll_hit(&bar, TRUE, &control->bar, point, &thumb);
    if (code >= 0) scroll(window, control, code, thumb);
    return 0;
  }
  const int index = item_at(window, control, point.y);
  if (index < 0) return 0;
  const BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  const BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  /* Win32: the double-click also selects the item under the cursor before
     LBN_DBLCLK, so the owner reads the clicked item, not a stale selection. */
  if (!doubleClick || !selected(window, control, index) || control->caretIndex != index)
    move_to(window, control, index, doubleClick ? FALSE : shift, doubleClick ? FALSE : ctrl, TRUE);
  if (doubleClick) notify(window, LBN_DBLCLK);
  return 0;
}

static LRESULT selection_message(HWND window, ControlRecord *control,
                                 UINT message, WPARAM wParam, LPARAM lParam) {
  const BOOL multi = multiple(ui_control_style(window));
  switch (message) {
    case LB_SETCURSEL: {
      if (multi) return LB_ERR;
      const int index = (int)wParam;
      if (index < -1 || index >= control->count) {
        control->current = -1;
        InvalidateRect(window, NULL, TRUE);
        return LB_ERR;
      }
      control->current = index;
      if (index >= 0) {
        control->caretIndex = index;
        ensure_visible(window, control, index);
      }
      InvalidateRect(window, NULL, TRUE);
      return index < 0 ? LB_ERR : index;
    }
    case LB_GETCURSEL:
      if (multi) return control->count ? control->caretIndex : LB_ERR;
      return control->current;
    case LB_SETSEL: {
      if (!multi) return LB_ERR;
      const int index = (int)lParam;
      if (index == -1) {
        select_range(control, 0, control->count - 1, wParam != 0);
      } else {
        if (index < 0 || index >= control->count) return LB_ERR;
        control->items[index].selected = wParam != 0;
      }
      InvalidateRect(window, NULL, TRUE);
      return LB_OKAY;
    }
    case LB_GETSEL: {
      const int index = (int)wParam;
      if (index < 0 || index >= control->count) return LB_ERR;
      return selected(window, control, index) ? 1 : 0;
    }
    case LB_GETSELCOUNT: {
      if (!multi) return LB_ERR;
      int n = 0;
      for (int i = 0; i < control->count; ++i) n += control->items[i].selected;
      return n;
    }
    case LB_GETSELITEMS: {
      if (!multi || !lParam) return LB_ERR;
      int *out = (int *)lParam;
      int n = 0;
      for (int i = 0; i < control->count && n < (int)wParam; ++i)
        if (control->items[i].selected) out[n++] = i;
      return n;
    }
    case LB_SETCARETINDEX: {
      const int index = (int)wParam;
      if (index < 0 || index >= control->count) return LB_ERR;
      control->caretIndex = index;
      if (!LOWORD(lParam)) ensure_visible(window, control, index);
      InvalidateRect(window, NULL, TRUE);
      return LB_OKAY;
    }
    case LB_GETCARETINDEX: return control->count ? control->caretIndex : LB_ERR;
    default: return LB_ERR;
  }
}

LRESULT CALLBACK ui_listbox_proc(HWND window, UINT message, WPARAM wParam,
                                 LPARAM lParam) {
  ControlRecord *control = ui_control(window);
  if (!control) return DefWindowProc(window, message, wParam, lParam);
  const DWORD style = ui_control_style(window);
  switch (message) {
    case WM_PAINT: paint(window, control); return 0;
    case WM_LBUTTONDOWN: return button_down(window, control, lParam, FALSE);
    case WM_LBUTTONDBLCLK: return button_down(window, control, lParam, TRUE);
    case WM_KEYDOWN: return key_down(window, control, wParam);
    case WM_CHAR: return character(window, control, wParam);
    case WM_VSCROLL:
      scroll(window, control, LOWORD(wParam), (SHORT)HIWORD(wParam));
      return 0;
    case WM_SETFOCUS:
      InvalidateRect(window, NULL, TRUE);
      notify(window, LBN_SETFOCUS);
      return 0;
    case WM_KILLFOCUS:
      InvalidateRect(window, NULL, TRUE);
      notify(window, LBN_KILLFOCUS);
      return 0;
    case WM_SIZE:
      set_top(window, control, control->top);
      sync_bar(window, control);
      return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case LB_ADDSTRING: return insert(window, control, -1, lParam, TRUE);
    case LB_INSERTSTRING:
      return insert(window, control, (int)wParam, lParam, FALSE);
    case LB_DELETESTRING: return remove_item(window, control, (int)wParam);
    case LB_RESETCONTENT: reset(window, control); return 0;
    case LB_GETCOUNT: return control->count;
    case LB_GETTEXT: {
      const int index = (int)wParam;
      if (index < 0 || index >= control->count || !lParam) return LB_ERR;
      if (!has_strings(style)) {
        memcpy((void *)lParam, &control->items[index].data, sizeof(ULONG_PTR));
        return sizeof(ULONG_PTR);
      }
      const size_t length = strlen(control->items[index].text);
      memcpy((char *)lParam, control->items[index].text, length + 1);
      return (LRESULT)length;
    }
    case LB_GETTEXTLEN: {
      const int index = (int)wParam;
      if (index < 0 || index >= control->count) return LB_ERR;
      if (!has_strings(style)) return sizeof(ULONG_PTR);
      return (LRESULT)strlen(control->items[index].text);
    }
    case LB_GETITEMDATA: {
      const int index = (int)wParam;
      if (index < 0 || index >= control->count) return LB_ERR;
      return (LRESULT)control->items[index].data;
    }
    case LB_SETITEMDATA: {
      const int index = (int)wParam;
      if (index < 0 || index >= control->count) return LB_ERR;
      control->items[index].data = (ULONG_PTR)lParam;
      return LB_OKAY;
    }
    case LB_FINDSTRING:
      return find(window, control, (int)wParam, lParam, FALSE);
    case LB_FINDSTRINGEXACT:
      return find(window, control, (int)wParam, lParam, TRUE);
    case LB_SELECTSTRING: {
      const int found = find(window, control, (int)wParam, lParam, FALSE);
      if (found < 0) return LB_ERR;
      if (multiple(style)) {
        control->items[found].selected = TRUE;
        control->caretIndex = found;
        ensure_visible(window, control, found);
        InvalidateRect(window, NULL, TRUE);
        return found;
      }
      return selection_message(window, control, LB_SETCURSEL, (WPARAM)found, 0);
    }
    case LB_GETTOPINDEX: return control->top;
    case LB_SETTOPINDEX:
      if ((int)wParam < 0 || (int)wParam >= control->count) return LB_ERR;
      set_top(window, control, (int)wParam);
      return LB_OKAY;
    case LB_GETITEMRECT:
      if (!lParam || !item_rect(window, control, (int)wParam, (RECT *)lParam))
        return LB_ERR;
      return LB_OKAY;
    case LB_GETITEMHEIGHT:
      if ((style & LBS_OWNERDRAWVARIABLE) &&
          ((int)wParam < 0 || (int)wParam >= control->count))
        return LB_ERR;
      return (LRESULT)item_height(control, (style & LBS_OWNERDRAWVARIABLE)
                                               ? (int)wParam : -1);
    case LB_SETITEMHEIGHT: {
      const UINT height = LOWORD(lParam);
      if (!height || height > 255) return LB_ERR;
      if (style & LBS_OWNERDRAWVARIABLE) {
        if ((int)wParam < 0 || (int)wParam >= control->count) return LB_ERR;
        control->items[wParam].height = height;
      } else {
        control->itemHeight = height;
      }
      sync_bar(window, control);
      InvalidateRect(window, NULL, TRUE);
      return LB_OKAY;
    }
    case LB_SETCURSEL:
    case LB_GETCURSEL:
    case LB_SETSEL:
    case LB_GETSEL:
    case LB_GETSELCOUNT:
    case LB_GETSELITEMS:
    case LB_SETCARETINDEX:
    case LB_GETCARETINDEX:
      return selection_message(window, control, message, wParam, lParam);
    case WM_SETFONT:
    case WM_GETFONT:
    case WM_ENABLE:
      return ui_control_text_message(control, message, wParam, lParam);
    default: return DefWindowProc(window, message, wParam, lParam);
  }
}
