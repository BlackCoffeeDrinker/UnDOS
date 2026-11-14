#ifndef UNDOSGUI_CONTROLS_H
#define UNDOSGUI_CONTROLS_H

#include <windows.h>

#include "win_internal.h"

#define UI_CONTROL_LIMIT 64
#define UI_CONTROL_TEXT_BYTES 256
#define UI_SCROLL_ARROW 16

typedef enum ControlKind {
  CONTROL_NONE = 0,
  CONTROL_BUTTON,
  CONTROL_STATIC,
  CONTROL_EDIT,
  CONTROL_LISTBOX,
  CONTROL_SCROLLBAR
} ControlKind;

typedef struct ListItem {
  char *text;
  ULONG_PTR data;
  UINT height;
  BOOL selected;
} ListItem;

typedef struct ScrollState {
  int minimum;
  int maximum;
  UINT page;
  int position;
} ScrollState;

typedef struct ControlRecord {
  BOOL used;
  uint32_t handle;
  ControlKind kind;
  char text[UI_CONTROL_TEXT_BYTES];
  HFONT font;
  /* BUTTON: BST_* bits. */
  UINT state;
  /* EDIT */
  UINT caret;
  UINT anchor;
  UINT limit;
  UINT scroll;
  BOOL modified;
  LONG charWidth;
  /* SCROLLBAR, and the LISTBOX's own vertical bar. */
  ScrollState bar;
  /* LISTBOX */
  ListItem *items;
  int count;
  int capacity;
  int current;
  int caretIndex;
  int top;
  int anchorIndex;
  UINT itemHeight;
} ControlRecord;

ControlRecord *ui_control(HWND window);
DWORD ui_control_style(HWND window);
void ui_control_notify(HWND window, UINT code);
LONG ui_control_line_height(void);
HFONT ui_control_select_font(HDC dc, ControlRecord *control);
void ui_control_edge(HDC dc, const RECT *rect, BOOL raised);
void ui_control_frame(HDC dc, const RECT *rect, int color);
void ui_control_fill(HDC dc, const RECT *rect, int color);
LRESULT ui_control_text_message(ControlRecord *control, UINT message,
                                WPARAM wParam, LPARAM lParam);

/* Scroll bar geometry shared by SCROLLBAR and LISTBOX. */
void ui_scroll_draw(HDC dc, const RECT *rect, BOOL vertical,
                    const ScrollState *bar, BOOL enabled);
int ui_scroll_hit(const RECT *rect, BOOL vertical, const ScrollState *bar,
                  POINT point, int *thumbPosition);
BOOL ui_scroll_apply(ScrollState *bar, LPCSCROLLINFO info);
void ui_scroll_read(const ScrollState *bar, LPSCROLLINFO info);
int ui_scroll_clamp(const ScrollState *bar, int position);

LRESULT CALLBACK ui_button_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ui_static_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ui_edit_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ui_listbox_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ui_scrollbar_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ui_dialog_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ui_mdiclient_proc(HWND, UINT, WPARAM, LPARAM);
void ui_listbox_release(ControlRecord *control);
void ui_scroll_window_forget(uint32_t handle);
void ui_scroll_window_reset(void);

#endif
