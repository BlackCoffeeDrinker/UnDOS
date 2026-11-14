#include <windows.h>
#include <undoscrt/string.h>

#include "../private/controls.h"
#include "../private/ui_internal.h"

/* SCROLLBAR control (SB_CTL) and the SB_HORZ/SB_VERT state of ordinary
   windows. Window scroll bars keep their range and position, but the
   compositor has no non-client scroll bars, so they are not drawn. */

#define UI_SCROLL_WINDOW_LIMIT 16

typedef struct WindowBars {
  uint32_t handle;
  ScrollState bars[2];
} WindowBars;

static WindowBars windowBars[UI_SCROLL_WINDOW_LIMIT];

void ui_scroll_window_reset(void) {
  memset(windowBars, 0, sizeof(windowBars));
}

void ui_scroll_window_forget(uint32_t handle) {
  for (size_t i = 0; i < UI_SCROLL_WINDOW_LIMIT; ++i)
    if (windowBars[i].handle == handle) windowBars[i].handle = 0;
}

static BOOL vertical(HWND window) {
  return (ui_control_style(window) & SBS_VERT) != 0;
}

static void paint(HWND window, ControlRecord *control) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(window, &ps);
  if (!dc) return;
  RECT client;
  GetClientRect(window, &client);
  ui_scroll_draw(dc, &client, vertical(window), &control->bar,
                 IsWindowEnabled(window));
  EndPaint(window, &ps);
}

LRESULT CALLBACK ui_scrollbar_proc(HWND window, UINT message, WPARAM wParam,
                                   LPARAM lParam) {
  ControlRecord *control = ui_control(window);
  if (!control) return DefWindowProc(window, message, wParam, lParam);
  ScrollState *bar = &control->bar;
  switch (message) {
    case WM_PAINT: paint(window, control); return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      if (!IsWindowEnabled(window)) return 0;
      RECT client;
      GetClientRect(window, &client);
      POINT point = {(SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam)};
      int thumb = bar->position;
      const int code = ui_scroll_hit(&client, vertical(window), bar, point,
                                     &thumb);
      if (code < 0) return 0;
      const UINT notice = vertical(window) ? WM_VSCROLL : WM_HSCROLL;
      const HWND parent = GetParent(window);
      SendMessage(parent, notice,
                  MAKEWPARAM(code, code == SB_THUMBPOSITION ? thumb : 0),
                  (LPARAM)window);
      SendMessage(parent, notice, SB_ENDSCROLL, (LPARAM)window);
      return 0;
    }
    case WM_KEYDOWN: {
      int code = -1;
      if (wParam == VK_UP || wParam == VK_LEFT) code = SB_LINEUP;
      else if (wParam == VK_DOWN || wParam == VK_RIGHT) code = SB_LINEDOWN;
      else if (wParam == VK_PRIOR) code = SB_PAGEUP;
      else if (wParam == VK_NEXT) code = SB_PAGEDOWN;
      else if (wParam == VK_HOME) code = SB_TOP;
      else if (wParam == VK_END) code = SB_BOTTOM;
      if (code >= 0)
        SendMessage(GetParent(window), vertical(window) ? WM_VSCROLL
                                                        : WM_HSCROLL,
                    MAKEWPARAM(code, 0), (LPARAM)window);
      return 0;
    }
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case SBM_SETPOS: {
      const int previous = bar->position;
      bar->position = ui_scroll_clamp(bar, (int)wParam);
      if (lParam) InvalidateRect(window, NULL, TRUE);
      return previous;
    }
    case SBM_GETPOS: return bar->position;
    case SBM_SETRANGE: {
      SCROLLINFO info = {sizeof(info), SIF_RANGE, (int)wParam, (int)lParam,
                         0, 0, 0};
      const int previous = bar->position;
      ui_scroll_apply(bar, &info);
      InvalidateRect(window, NULL, TRUE);
      return previous;
    }
    case SBM_GETRANGE:
      if (wParam) *(int *)wParam = bar->minimum;
      if (lParam) *(int *)lParam = bar->maximum;
      return 0;
    case SBM_SETSCROLLINFO:
      if (!lParam) return 0;
      ui_scroll_apply(bar, (LPCSCROLLINFO)lParam);
      if (wParam) InvalidateRect(window, NULL, TRUE);
      return bar->position;
    case SBM_GETSCROLLINFO:
      if (!lParam) return FALSE;
      ui_scroll_read(bar, (LPSCROLLINFO)lParam);
      return TRUE;
    case WM_ENABLE:
      return ui_control_text_message(control, message, wParam, lParam);
    default: return DefWindowProc(window, message, wParam, lParam);
  }
}

/* Resolves the state behind (window, bar); NULL sets the Win32 error. */
static ScrollState *state_for(HWND window, int which, BOOL create,
                              BOOL *isControl) {
  *isControl = FALSE;
  if (!IsWindow(window)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  if (which == SB_CTL) {
    ControlRecord *control = ui_control(window);
    if (!control || control->kind != CONTROL_SCROLLBAR) {
      local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      return NULL;
    }
    *isControl = TRUE;
    return &control->bar;
  }
  if (which != SB_HORZ && which != SB_VERT) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  const uint32_t handle = hwnd_value(window);
  for (size_t i = 0; i < UI_SCROLL_WINDOW_LIMIT; ++i)
    if (windowBars[i].handle == handle) return &windowBars[i].bars[which];
  if (!create) {
    static ScrollState standard;
    standard.minimum = 0;
    standard.maximum = 100;
    standard.page = 0;
    standard.position = 0;
    return &standard;
  }
  for (size_t i = 0; i < UI_SCROLL_WINDOW_LIMIT; ++i) {
    if (windowBars[i].handle) continue;
    windowBars[i].handle = handle;
    for (int b = 0; b < 2; ++b) {
      windowBars[i].bars[b].minimum = 0;
      windowBars[i].bars[b].maximum = 100;
      windowBars[i].bars[b].page = 0;
      windowBars[i].bars[b].position = 0;
    }
    return &windowBars[i].bars[which];
  }
  local_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
  return NULL;
}

int SetScrollInfo(HWND window, int which, LPCSCROLLINFO info, BOOL redraw) {
  BOOL isControl;
  if (!info) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  ScrollState *bar = state_for(window, which, TRUE, &isControl);
  if (!bar) return 0;
  ui_scroll_apply(bar, info);
  if (redraw && isControl) InvalidateRect(window, NULL, TRUE);
  return bar->position;
}

BOOL GetScrollInfo(HWND window, int which, LPSCROLLINFO info) {
  BOOL isControl;
  if (!info) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ScrollState *bar = state_for(window, which, FALSE, &isControl);
  if (!bar) return FALSE;
  ui_scroll_read(bar, info);
  return TRUE;
}

int SetScrollPos(HWND window, int which, int position, BOOL redraw) {
  BOOL isControl;
  ScrollState *bar = state_for(window, which, TRUE, &isControl);
  if (!bar) return 0;
  const int previous = bar->position;
  bar->position = ui_scroll_clamp(bar, position);
  if (redraw && isControl) InvalidateRect(window, NULL, TRUE);
  return previous;
}

int GetScrollPos(HWND window, int which) {
  BOOL isControl;
  ScrollState *bar = state_for(window, which, FALSE, &isControl);
  return bar ? bar->position : 0;
}

BOOL SetScrollRange(HWND window, int which, int minimum, int maximum,
                    BOOL redraw) {
  BOOL isControl;
  ScrollState *bar = state_for(window, which, TRUE, &isControl);
  if (!bar) return FALSE;
  SCROLLINFO info = {sizeof(info), SIF_RANGE, minimum, maximum, 0, 0, 0};
  ui_scroll_apply(bar, &info);
  if (redraw && isControl) InvalidateRect(window, NULL, TRUE);
  return TRUE;
}

BOOL GetScrollRange(HWND window, int which, int *minimum, int *maximum) {
  BOOL isControl;
  if (!minimum || !maximum) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ScrollState *bar = state_for(window, which, FALSE, &isControl);
  if (!bar) return FALSE;
  *minimum = bar->minimum;
  *maximum = bar->maximum;
  return TRUE;
}
