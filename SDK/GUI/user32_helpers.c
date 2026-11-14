#include <windows.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/win_internal.h"

/* Rectangle arithmetic, coordinate mapping, SetWindowPos and MessageBox.
   Window rectangles from the kernel are in screen coordinates; the client
   area sits inside a border of equal width on the left, right and bottom,
   with the caption and menu bar on top. */

static BOOL bad_parameter(void) {
  local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
  return FALSE;
}

BOOL CopyRect(RECT *destination, const RECT *source) {
  if (!destination || !source) return bad_parameter();
  *destination = *source;
  return TRUE;
}

BOOL SetRectEmpty(RECT *rect) {
  if (!rect) return bad_parameter();
  rect->left = rect->top = rect->right = rect->bottom = 0;
  return TRUE;
}

BOOL IsRectEmpty(const RECT *rect) {
  return !rect || rect->right <= rect->left || rect->bottom <= rect->top;
}

BOOL EqualRect(const RECT *a, const RECT *b) {
  if (!a || !b) return FALSE;
  return a->left == b->left && a->top == b->top && a->right == b->right &&
         a->bottom == b->bottom;
}

BOOL PtInRect(const RECT *rect, POINT point) {
  return rect && point.x >= rect->left && point.x < rect->right &&
         point.y >= rect->top && point.y < rect->bottom;
}

BOOL IntersectRect(RECT *destination, const RECT *a, const RECT *b) {
  if (!destination || !a || !b) return bad_parameter();
  RECT result = {a->left > b->left ? a->left : b->left,
                 a->top > b->top ? a->top : b->top,
                 a->right < b->right ? a->right : b->right,
                 a->bottom < b->bottom ? a->bottom : b->bottom};
  if (IsRectEmpty(&result)) {
    SetRectEmpty(destination);
    return FALSE;
  }
  *destination = result;
  return TRUE;
}

BOOL UnionRect(RECT *destination, const RECT *a, const RECT *b) {
  if (!destination || !a || !b) return bad_parameter();
  if (IsRectEmpty(a) && IsRectEmpty(b)) {
    SetRectEmpty(destination);
    return FALSE;
  }
  if (IsRectEmpty(a)) { *destination = *b; return TRUE; }
  if (IsRectEmpty(b)) { *destination = *a; return TRUE; }
  RECT result = {a->left < b->left ? a->left : b->left,
                 a->top < b->top ? a->top : b->top,
                 a->right > b->right ? a->right : b->right,
                 a->bottom > b->bottom ? a->bottom : b->bottom};
  *destination = result;
  return TRUE;
}

int FrameRect(HDC dc, const RECT *rect, HBRUSH brush) {
  if (!rect) return bad_parameter();
  if (IsRectEmpty(rect)) return TRUE;
  RECT edge = {rect->left, rect->top, rect->right, rect->top + 1};
  if (!FillRect(dc, &edge, brush)) return FALSE;
  edge.top = rect->bottom - 1;
  edge.bottom = rect->bottom;
  if (!FillRect(dc, &edge, brush)) return FALSE;
  edge = (RECT){rect->left, rect->top, rect->left + 1, rect->bottom};
  if (!FillRect(dc, &edge, brush)) return FALSE;
  edge.left = rect->right - 1;
  edge.right = rect->right;
  return FillRect(dc, &edge, brush);
}

/* Screen position of the client area's top-left corner. */
static BOOL client_origin(HWND window, POINT *origin) {
  RECT outer, inner;
  if (!GetWindowRect(window, &outer) || !GetClientRect(window, &inner))
    return FALSE;
  const LONG border = ((outer.right - outer.left) - inner.right) / 2;
  origin->x = outer.left + border;
  origin->y = outer.bottom - border - inner.bottom;
  return TRUE;
}

BOOL ClientToScreen(HWND window, POINT *point) {
  if (!point) return bad_parameter();
  POINT origin;
  if (!client_origin(window, &origin)) return FALSE;
  point->x += origin.x;
  point->y += origin.y;
  return TRUE;
}

BOOL ScreenToClient(HWND window, POINT *point) {
  if (!point) return bad_parameter();
  POINT origin;
  if (!client_origin(window, &origin)) return FALSE;
  point->x -= origin.x;
  point->y -= origin.y;
  return TRUE;
}

/* A NULL window stands for the screen. Returns the vertical offset in the
   high word and the horizontal offset in the low word, as Win32 does. */
int MapWindowPoints(HWND from, HWND to, POINT *points, UINT count) {
  if (!points && count) return bad_parameter();
  POINT a = {0, 0}, b = {0, 0};
  SetLastError(ERROR_SUCCESS);
  if (from && !client_origin(from, &a)) return 0;
  if (to && !client_origin(to, &b)) return 0;
  const LONG dx = a.x - b.x;
  const LONG dy = a.y - b.y;
  for (UINT i = 0; i < count; ++i) {
    points[i].x += dx;
    points[i].y += dy;
  }
  return (int)MAKELONG((WORD)(SHORT)dx, (WORD)(SHORT)dy);
}

/* The kernel keeps no window minimized state for the SDK to read, but a
   minimized window is the only kind with a window rectangle and no client
   area. */
BOOL IsIconic(HWND window) {
  RECT outer, inner;
  if (!GetWindowRect(window, &outer) || !GetClientRect(window, &inner))
    return FALSE;
  return !IsRectEmpty(&outer) && IsRectEmpty(&inner);
}

/* The kernel repaints through the queue; WM_PAINT arrives with the next
   GetMessage, so there is nothing to force here. */
BOOL UpdateWindow(HWND window) {
  if (!IsWindow(window)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  return TRUE;
}

/* Z-order is not exposed by the kernel, so insertAfter is accepted and
   ignored. Position and size are taken relative to the parent's client
   area, as MoveWindow takes them. */
BOOL SetWindowPos(HWND window, HWND insertAfter, int x, int y, int width,
                  int height, UINT flags) {
  (void)insertAfter;
  if (!IsWindow(window)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if ((flags & SWP_SHOWWINDOW) && (flags & SWP_HIDEWINDOW)) return bad_parameter();
  if ((flags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE)) {
    RECT rect;
    if (!GetWindowRect(window, &rect)) return FALSE;
    if (flags & SWP_NOMOVE) {
      POINT corner = {rect.left, rect.top};
      HWND parent = GetParent(window);
      if (parent) ScreenToClient(parent, &corner);
      x = corner.x;
      y = corner.y;
    }
    if (flags & SWP_NOSIZE) {
      width = rect.right - rect.left;
      height = rect.bottom - rect.top;
    }
    if (!MoveWindow(window, x, y, width, height, !(flags & SWP_NOREDRAW)))
      return FALSE;
  }
  if (flags & SWP_SHOWWINDOW) ShowWindow(window, SW_SHOW);
  if (flags & SWP_HIDEWINDOW) ShowWindow(window, SW_HIDE);
  return TRUE;
}

BOOL MessageBeep(UINT type) {
  (void)type;
  return TRUE;
}

/* There is no window DC outside BeginPaint in the kernel yet. */
/* MessageBox builds a DLGTEMPLATE in memory: one static text item and a
   row of buttons, laid out in dialog units. */
#define MB_TEMPLATE_WORDS 2048
#define MB_TEXT_ID 0xFFFF

typedef struct MessageBoxLayout {
  WORD words[MB_TEMPLATE_WORDS];
  size_t used;
  BOOL overflow;
} MessageBoxLayout;

static void put_word(MessageBoxLayout *layout, WORD value) {
  if (layout->used >= MB_TEMPLATE_WORDS) {
    layout->overflow = TRUE;
    return;
  }
  layout->words[layout->used++] = value;
}

static void put_dword(MessageBoxLayout *layout, DWORD value) {
  put_word(layout, LOWORD(value));
  put_word(layout, HIWORD(value));
}

/* UTF-8 to UTF-16; malformed bytes become '?'. */
static void put_text(MessageBoxLayout *layout, const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  while (*p) {
    uint32_t c = *p++;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (c >= 0x80 && c < 0xC0) c = '?';
    else if (extra) {
      c &= 0x3Fu >> extra;
      for (; extra && (*p & 0xC0) == 0x80; --extra) c = (c << 6) | (*p++ & 0x3F);
      if (extra) c = '?';
    }
    if (c >= 0x10000) {
      c -= 0x10000;
      put_word(layout, (WORD)(0xD800 + (c >> 10)));
      put_word(layout, (WORD)(0xDC00 + (c & 0x3FF)));
    } else put_word(layout, (WORD)c);
  }
  put_word(layout, 0);
}

static void align_dword(MessageBoxLayout *layout) {
  if (layout->used & 1) put_word(layout, 0);
}

static void put_item(MessageBoxLayout *layout, DWORD style, short x, short y,
                     short cx, short cy, WORD id, WORD classOrdinal,
                     const char *text) {
  align_dword(layout);
  put_dword(layout, style | WS_CHILD | WS_VISIBLE);
  put_dword(layout, 0);
  put_word(layout, (WORD)x);
  put_word(layout, (WORD)y);
  put_word(layout, (WORD)cx);
  put_word(layout, (WORD)cy);
  put_word(layout, id);
  put_word(layout, 0xFFFF);
  put_word(layout, classOrdinal);
  put_text(layout, text);
  put_word(layout, 0);
}

typedef struct MessageBoxButtons {
  int count;
  int ids[3];
  const char *labels[3];
  BOOL cancellable;
} MessageBoxButtons;

static BOOL buttons_for(UINT type, MessageBoxButtons *buttons) {
  static const int ids[][3] = {
      {IDOK}, {IDOK, IDCANCEL}, {IDABORT, IDRETRY, IDIGNORE},
      {IDYES, IDNO, IDCANCEL}, {IDYES, IDNO}, {IDRETRY, IDCANCEL}};
  static const int counts[] = {1, 2, 3, 3, 2, 2};
  const UINT kind = type & MB_TYPEMASK;
  if (kind > MB_RETRYCANCEL) return FALSE;
  buttons->count = counts[kind];
  buttons->cancellable = FALSE;
  for (int i = 0; i < buttons->count; ++i) {
    const int id = ids[kind][i];
    buttons->ids[i] = id;
    if (id == IDCANCEL) buttons->cancellable = TRUE;
    buttons->labels[i] = id == IDOK ? "OK" : id == IDCANCEL ? "Cancel"
        : id == IDABORT ? "&Abort" : id == IDRETRY ? "&Retry"
        : id == IDIGNORE ? "&Ignore" : id == IDYES ? "&Yes" : "&No";
  }
  /* A lone OK button also closes on Esc. */
  if (kind == MB_OK) buttons->cancellable = TRUE;
  return TRUE;
}

typedef struct MessageBoxState {
  MessageBoxButtons buttons;
  int defaultId;
} MessageBoxState;

static INT_PTR CALLBACK message_box_proc(HWND dialog, UINT message,
                                         WPARAM wParam, LPARAM lParam) {
  if (message == WM_INITDIALOG) {
    SetWindowLongPtr(dialog, DWLP_USER, lParam);
    const MessageBoxState *initial = (const MessageBoxState *)lParam;
    HWND button = GetDlgItem(dialog, initial->defaultId);
    if (!button) return TRUE;
    SetFocus(button);
    return FALSE;
  }
  if (message != WM_COMMAND) return FALSE;
  const MessageBoxState *state =
      (const MessageBoxState *)GetWindowLongPtr(dialog, DWLP_USER);
  const int id = LOWORD(wParam);
  for (int i = 0; state && i < state->buttons.count; ++i) {
    if (state->buttons.ids[i] == id) {
      EndDialog(dialog, id);
      return TRUE;
    }
  }
  if (id == IDCANCEL && state && state->buttons.cancellable) {
    EndDialog(dialog, state->buttons.ids[state->buttons.count - 1] == IDCANCEL
                          ? IDCANCEL : state->buttons.ids[0]);
    return TRUE;
  }
  return FALSE;
}

int MessageBox(HWND owner, LPCSTR text, LPCSTR caption, UINT type) {
  MessageBoxState state;
  if (!buttons_for(type, &state.buttons)) {
    bad_parameter();
    return 0;
  }
  if (!text) text = "";
  if (!caption) caption = "Error";
  int lines = 1, column = 0, longest = 0;
  for (const char *p = text; *p; ++p) {
    if (*p == '\n') {
      ++lines;
      column = 0;
    } else if (++column > longest) longest = column;
  }
  const int buttonWidth = 40, buttonGap = 6, margin = 8;
  const int buttonsWidth = state.buttons.count * buttonWidth +
                           (state.buttons.count - 1) * buttonGap;
  int textWidth = longest * 4;
  if (textWidth > 280) {
    lines += textWidth / 280;
    textWidth = 280;
  }
  const int width = (textWidth > buttonsWidth ? textWidth : buttonsWidth) + 2 * margin;
  const int textHeight = lines * 8;
  const int height = margin + textHeight + margin + 14 + margin;
  UINT defaultIndex = (type & MB_DEFMASK) >> 8;
  if (defaultIndex >= (UINT)state.buttons.count) defaultIndex = 0;
  state.defaultId = state.buttons.ids[defaultIndex];

  MessageBoxLayout *layout = (MessageBoxLayout *)LocalAlloc(LMEM_ZEROINIT, sizeof(MessageBoxLayout));
  if (!layout) return 0;
  put_dword(layout, WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME);
  put_dword(layout, 0);
  put_word(layout, (WORD)(1 + state.buttons.count));
  put_word(layout, 0);
  put_word(layout, 0);
  put_word(layout, (WORD)width);
  put_word(layout, (WORD)height);
  put_word(layout, 0);
  put_word(layout, 0);
  put_text(layout, caption);
  put_item(layout, SS_LEFT, margin, margin, (short)textWidth, (short)textHeight,
           MB_TEXT_ID, 0x0082, text);
  int x = (width - buttonsWidth) / 2;
  for (int i = 0; i < state.buttons.count; ++i) {
    const DWORD style = WS_TABSTOP | ((UINT)i == defaultIndex ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON) |
                        (i == 0 ? WS_GROUP : 0);
    put_item(layout, style, (short)x, (short)(height - margin - 14),
             (short)buttonWidth, 14, (WORD)state.buttons.ids[i], 0x0080,
             state.buttons.labels[i]);
    x += buttonWidth + buttonGap;
  }
  int result = 0;
  if (layout->overflow) bad_parameter();
  else {
    const INT_PTR chosen = DialogBoxIndirectParam(
        NULL, (LPCDLGTEMPLATE)layout->words, owner, message_box_proc,
        (LPARAM)&state);
    result = chosen > 0 ? (int)chosen : 0;
  }
  LocalFree(layout);
  return result;
}
