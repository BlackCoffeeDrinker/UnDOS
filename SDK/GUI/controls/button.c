#include <windows.h>
#include <undoscrt/string.h>

#include "../private/controls.h"

/* BUTTON: push, default push, check boxes, radio buttons, group boxes and
   owner-drawn buttons. */

#define CHECK_BOX 13

static UINT type_of(HWND window) {
  return ui_control_style(window) & BS_TYPEMASK;
}

static BOOL is_check(UINT type) {
  return type == BS_CHECKBOX || type == BS_AUTOCHECKBOX ||
         type == BS_3STATE || type == BS_AUTO3STATE;
}

static BOOL is_radio(UINT type) {
  return type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
}

static void draw_label(HDC dc, ControlRecord *control, RECT *rect, UINT format,
                       BOOL enabled) {
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
  DrawText(dc, control->text, -1, rect, format | DT_SINGLELINE | DT_VCENTER);
}

static void paint_owner(HWND window, HDC dc, const RECT *client,
                        ControlRecord *control, BOOL enabled) {
  DRAWITEMSTRUCT item = {0};
  item.CtlType = ODT_BUTTON;
  item.CtlID = (UINT)GetDlgCtrlID(window);
  item.itemAction = ODA_DRAWENTIRE;
  if (control->state & BST_PUSHED) item.itemState |= ODS_SELECTED;
  if (control->state & BST_FOCUS) item.itemState |= ODS_FOCUS;
  if (!enabled) item.itemState |= ODS_DISABLED;
  item.hwndItem = window;
  item.hDC = dc;
  item.rcItem = *client;
  (void)SendMessage(GetParent(window), WM_DRAWITEM, item.CtlID,
                    (LPARAM)&item);
}

static void paint(HWND window, ControlRecord *control) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(window, &ps);
  if (!dc) return;
  RECT client;
  GetClientRect(window, &client);
  const UINT type = type_of(window);
  const BOOL enabled = IsWindowEnabled(window);
  HFONT previous = ui_control_select_font(dc, control);
  if (type == BS_OWNERDRAW) {
    paint_owner(window, dc, &client, control, enabled);
  } else if (type == BS_GROUPBOX) {
    ui_control_fill(dc, &client, COLOR_BTNFACE);
    const LONG half = ui_control_line_height() / 2;
    RECT frame = client;
    frame.top += half;
    ui_control_frame(dc, &frame, COLOR_BTNSHADOW);
    SIZE size = {0, 0};
    GetTextExtentPoint32(dc, control->text, (int)strlen(control->text), &size);
    RECT label;
    SetRect(&label, client.left + 6, client.top, client.left + 10 + size.cx,
            client.top + 2 * half);
    if (control->text[0]) {
      ui_control_fill(dc, &label, COLOR_BTNFACE);
      label.left += 2;
      draw_label(dc, control, &label, DT_LEFT, enabled);
    }
  } else if (is_check(type) || is_radio(type)) {
    ui_control_fill(dc, &client, COLOR_BTNFACE);
    const BOOL leftText = (ui_control_style(window) & BS_LEFTTEXT) != 0;
    const LONG top = (client.top + client.bottom - CHECK_BOX) / 2;
    const LONG left = leftText ? client.right - CHECK_BOX : client.left;
    RECT box;
    SetRect(&box, left, top, left + CHECK_BOX, top + CHECK_BOX);
    ui_control_fill(dc, &box, (control->state & BST_PUSHED) ? COLOR_BTNFACE
                                                            : COLOR_WINDOW);
    ui_control_frame(dc, &box, COLOR_WINDOWFRAME);
    const UINT check = control->state & 3u;
    if (check && is_radio(type)) {
      RECT dot = box;
      InflateRect(&dot, -4, -4);
      ui_control_fill(dc, &dot, COLOR_WINDOWTEXT);
    } else if (check == BST_CHECKED) {
      SelectObject(dc, GetStockObject(BLACK_PEN));
      MoveToEx(dc, box.left + 2, box.top + 2, NULL);
      LineTo(dc, box.right - 2, box.bottom - 2);
      MoveToEx(dc, box.right - 3, box.top + 2, NULL);
      LineTo(dc, box.left + 1, box.bottom - 2);
    } else if (check == BST_INDETERMINATE) {
      RECT fill = box;
      InflateRect(&fill, -2, -2);
      ui_control_fill(dc, &fill, COLOR_BTNSHADOW);
    }
    RECT label = client;
    if (leftText) label.right = box.left - 4;
    else label.left = box.right + 4;
    draw_label(dc, control, &label, DT_LEFT, enabled);
    if (control->state & BST_FOCUS) {
      SIZE size = {0, 0};
      GetTextExtentPoint32(dc, control->text, (int)strlen(control->text),
                           &size);
      RECT focus;
      SetRect(&focus, label.left - 1, label.top, label.left + size.cx + 1,
              label.bottom);
      DrawFocusRect(dc, &focus);
    }
  } else {
    const BOOL pushed = (control->state & BST_PUSHED) != 0;
    RECT face = client;
    ui_control_frame(dc, &face, COLOR_WINDOWFRAME);
    InflateRect(&face, -1, -1);
    if (type == BS_DEFPUSHBUTTON) {
      ui_control_frame(dc, &face, COLOR_WINDOWFRAME);
      InflateRect(&face, -1, -1);
    }
    ui_control_fill(dc, &face, COLOR_BTNFACE);
    ui_control_edge(dc, &face, !pushed);
    RECT label = face;
    if (pushed) OffsetRect(&label, 1, 1);
    draw_label(dc, control, &label, DT_CENTER, enabled);
    if (control->state & BST_FOCUS) {
      RECT focus = face;
      InflateRect(&focus, -3, -3);
      DrawFocusRect(dc, &focus);
    }
  }
  if (previous) SelectObject(dc, previous);
  EndPaint(window, &ps);
}

static void set_check(HWND window, ControlRecord *control, UINT check) {
  if ((control->state & 3u) == (check & 3u)) return;
  control->state = (control->state & ~3u) | (check & 3u);
  InvalidateRect(window, NULL, TRUE);
}

/* An auto radio button unchecks the other radio buttons of its group. */
static void check_radio_group(HWND window) {
  HWND parent = GetParent(window);
  if (!parent) return;
  HWND dialogStart = window;
  while (!(ui_control_style(dialogStart) & WS_GROUP)) {
    HWND previous = GetWindow(dialogStart, GW_HWNDPREV);
    if (!previous) break;
    dialogStart = previous;
  }
  for (HWND item = dialogStart; item; item = GetWindow(item, GW_HWNDNEXT)) {
    if (item != dialogStart && (ui_control_style(item) & WS_GROUP)) break;
    ControlRecord *other = ui_control(item);
    if (other && other->kind == CONTROL_BUTTON &&
        type_of(item) == BS_AUTORADIOBUTTON)
      set_check(item, other, item == window ? BST_CHECKED : BST_UNCHECKED);
  }
}

static void click(HWND window, ControlRecord *control) {
  const UINT type = type_of(window);
  if (type == BS_AUTOCHECKBOX)
    set_check(window, control, (control->state & BST_CHECKED) ? 0 : 1);
  else if (type == BS_AUTO3STATE)
    set_check(window, control, ((control->state & 3u) + 1u) % 3u);
  else if (type == BS_AUTORADIOBUTTON)
    check_radio_group(window);
  ui_control_notify(window, BN_CLICKED);
}

static void set_pushed(HWND window, ControlRecord *control, BOOL pushed) {
  const UINT state = pushed ? (control->state | BST_PUSHED)
                            : (control->state & ~(UINT)BST_PUSHED);
  if (state == control->state) return;
  control->state = state;
  InvalidateRect(window, NULL, TRUE);
}

LRESULT CALLBACK ui_button_proc(HWND window, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  ControlRecord *control = ui_control(window);
  if (!control) return DefWindowProc(window, message, wParam, lParam);
  const UINT type = type_of(window);
  switch (message) {
    case WM_PAINT: paint(window, control); return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      if (type == BS_GROUPBOX || !IsWindowEnabled(window)) return 0;
      SetFocus(window);
      set_pushed(window, control, TRUE);
      return 0;
    case WM_LBUTTONUP:
      if (!(control->state & BST_PUSHED)) return 0;
      set_pushed(window, control, FALSE);
      click(window, control);
      return 0;
    case WM_KEYDOWN:
      if (wParam == VK_SPACE && type != BS_GROUPBOX)
        set_pushed(window, control, TRUE);
      return 0;
    case WM_KEYUP:
      if (wParam == VK_SPACE && (control->state & BST_PUSHED)) {
        set_pushed(window, control, FALSE);
        click(window, control);
      }
      return 0;
    case WM_SETFOCUS:
      control->state |= BST_FOCUS;
      InvalidateRect(window, NULL, TRUE);
      return 0;
    case WM_KILLFOCUS:
      control->state &= ~(UINT)(BST_FOCUS | BST_PUSHED);
      InvalidateRect(window, NULL, TRUE);
      return 0;
    case WM_GETDLGCODE:
      if (type == BS_GROUPBOX) return DLGC_STATIC;
      if (is_radio(type)) return DLGC_BUTTON | DLGC_RADIOBUTTON;
      if (type == BS_DEFPUSHBUTTON) return DLGC_BUTTON | DLGC_DEFPUSHBUTTON;
      if (type == BS_PUSHBUTTON) return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
      return DLGC_BUTTON;
    case BM_GETCHECK: return (LRESULT)(control->state & 3u);
    case BM_SETCHECK:
      if (is_check(type) || is_radio(type)) set_check(window, control,
                                                      (UINT)wParam);
      return 0;
    case BM_GETSTATE: return (LRESULT)control->state;
    case BM_SETSTATE: set_pushed(window, control, wParam != 0); return 0;
    case BM_SETSTYLE: {
      const DWORD style = ui_control_style(window);
      SetWindowLong(window, GWL_STYLE,
                    (LONG)((style & ~BS_TYPEMASK) | ((DWORD)wParam & 0xFFFFu)));
      if (LOWORD(lParam)) InvalidateRect(window, NULL, TRUE);
      return 0;
    }
    case BM_CLICK:
      if (IsWindowEnabled(window)) click(window, control);
      return 0;
    case WM_SETTEXT:
    case WM_GETTEXT:
    case WM_GETTEXTLENGTH:
    case WM_SETFONT:
    case WM_GETFONT:
    case WM_ENABLE:
      return ui_control_text_message(control, message, wParam, lParam);
    default: return DefWindowProc(window, message, wParam, lParam);
  }
}
