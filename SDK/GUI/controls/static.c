#include <windows.h>

#include "../private/controls.h"

/* STATIC: text labels plus the filled rectangle and frame styles. */

static void paint(HWND window, ControlRecord *control) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(window, &ps);
  if (!dc) return;
  RECT client;
  GetClientRect(window, &client);
  const DWORD style = ui_control_style(window);
  const UINT type = style & SS_TYPEMASK;
  switch (type) {
    case SS_BLACKRECT: ui_control_fill(dc, &client, COLOR_WINDOWFRAME); break;
    case SS_GRAYRECT: ui_control_fill(dc, &client, COLOR_BTNSHADOW); break;
    case SS_WHITERECT: ui_control_fill(dc, &client, COLOR_WINDOW); break;
    case SS_BLACKFRAME: ui_control_frame(dc, &client, COLOR_WINDOWFRAME); break;
    case SS_GRAYFRAME: ui_control_frame(dc, &client, COLOR_BTNSHADOW); break;
    case SS_WHITEFRAME: ui_control_frame(dc, &client, COLOR_WINDOW); break;
    default: {
      ui_control_fill(dc, &client, COLOR_BTNFACE);
      HFONT previous = ui_control_select_font(dc, control);
      UINT format = DT_LEFT | DT_WORDBREAK;
      if (type == SS_CENTER) format = DT_CENTER | DT_WORDBREAK;
      else if (type == SS_RIGHT) format = DT_RIGHT | DT_WORDBREAK;
      else if (type == SS_SIMPLE || type == SS_LEFTNOWORDWRAP)
        format = DT_LEFT | DT_SINGLELINE;
      if (style & SS_NOPREFIX) format |= DT_NOPREFIX;
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, GetSysColor(IsWindowEnabled(window) ? COLOR_BTNTEXT
                                                           : COLOR_GRAYTEXT));
      DrawText(dc, control->text, -1, &client, format);
      if (previous) SelectObject(dc, previous);
      break;
    }
  }
  EndPaint(window, &ps);
}

LRESULT CALLBACK ui_static_proc(HWND window, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  ControlRecord *control = ui_control(window);
  if (!control) return DefWindowProc(window, message, wParam, lParam);
  switch (message) {
    case WM_PAINT: paint(window, control); return 0;
    case WM_GETDLGCODE: return DLGC_STATIC;
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
