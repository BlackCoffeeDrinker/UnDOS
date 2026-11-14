#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "../private/controls.h"
#include "../private/ui_internal.h"

/* Built-in window classes, registered on first CreateWindow, and the
   state and drawing helpers they share. */

typedef struct BuiltinClass {
  const char *name;
  WNDPROC procedure;
  ControlKind kind;
} BuiltinClass;

static const BuiltinClass builtins[] = {
    {WC_BUTTON, ui_button_proc, CONTROL_BUTTON},
    {WC_STATIC, ui_static_proc, CONTROL_STATIC},
    {WC_EDIT, ui_edit_proc, CONTROL_EDIT},
    {WC_LISTBOX, ui_listbox_proc, CONTROL_LISTBOX},
    {WC_SCROLLBAR, ui_scrollbar_proc, CONTROL_SCROLLBAR},
    {WC_DIALOG, ui_dialog_proc, CONTROL_NONE},
    {WC_MDICLIENT, ui_mdiclient_proc, CONTROL_NONE},
};
#define BUILTIN_COUNT (sizeof(builtins) / sizeof(builtins[0]))
#define SYS_COLOR_COUNT 21

static ControlRecord controls[UI_CONTROL_LIMIT];
static HBRUSH sysBrushes[SYS_COLOR_COUNT];
static LONG lineHeight;

static const COLORREF sysColors[SYS_COLOR_COUNT] = {
    RGB(192, 192, 192), RGB(0, 128, 128),   RGB(0, 0, 128),
    RGB(128, 128, 128), RGB(192, 192, 192), RGB(255, 255, 255),
    RGB(0, 0, 0),       RGB(0, 0, 0),       RGB(0, 0, 0),
    RGB(255, 255, 255), RGB(192, 192, 192), RGB(192, 192, 192),
    RGB(128, 128, 128), RGB(0, 0, 128),     RGB(255, 255, 255),
    RGB(192, 192, 192), RGB(128, 128, 128), RGB(128, 128, 128),
    RGB(0, 0, 0),       RGB(192, 192, 192), RGB(255, 255, 255),
};

static char fold(char c) {
  return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static BOOL same_class(const char *a, const char *b) {
  for (; *a && *b; ++a, ++b)
    if (fold(*a) != fold(*b)) return FALSE;
  return *a == *b;
}

const char *ui_controls_class_name(const char *name) {
  for (size_t i = 0; i < BUILTIN_COUNT; ++i) {
    if (!same_class(name, builtins[i].name)) continue;
    if (!class_by_name(builtins[i].name)) {
      WNDCLASS wc = {0};
      wc.lpfnWndProc = builtins[i].procedure;
      wc.lpszClassName = builtins[i].name;
      (void)RegisterClass(&wc);
    }
    return builtins[i].name;
  }
  return name;
}

static ControlKind kind_of(WNDPROC procedure) {
  for (size_t i = 0; i < BUILTIN_COUNT; ++i)
    if (builtins[i].procedure == procedure) return builtins[i].kind;
  return CONTROL_NONE;
}

LONG ui_control_line_height(void) {
  if (lineHeight) return lineHeight;
  UNDOS_FONT font;
  uint32_t generation;
  UNDOS_TEXT_METRICS metrics;
  const DWORD error = GetLastError();
  lineHeight = 16;
  if (UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT, UNDOS_GLYPH_FALLBACK_NOTDEF,
                       &font, &generation)) {
    if (UndosGetFontMetrics(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_BASIC,
                            &metrics) &&
        metrics.lineHeight)
      lineHeight = (LONG)metrics.lineHeight;
    (void)UndosCloseFont(font);
  }
  SetLastError(error);
  return lineHeight;
}

void ui_controls_attach(HWND window, const char *text) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  if (!record) return;
  const ControlKind kind = kind_of(record->procedure);
  if (kind == CONTROL_NONE) return;
  ControlRecord *control = NULL;
  for (size_t i = 0; i < UI_CONTROL_LIMIT && !control; ++i)
    if (!controls[i].used) control = &controls[i];
  if (!control) return;
  memset(control, 0, sizeof(*control));
  control->used = TRUE;
  control->handle = record->handle;
  control->kind = kind;
  ui_copy_text(control->text, sizeof(control->text), text);
  control->limit = UI_CONTROL_TEXT_BYTES - 1;
  control->current = -1;
  control->itemHeight = (UINT)ui_control_line_height();
  if (kind == CONTROL_EDIT) control->caret = control->anchor =
      (UINT)strlen(control->text);
  if (kind == CONTROL_LISTBOX &&
      (record->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE))) {
    MEASUREITEMSTRUCT measure = {0};
    measure.CtlType = ODT_LISTBOX;
    measure.CtlID = record->id;
    measure.itemHeight = control->itemHeight;
    (void)SendMessage(hwnd_from_value(record->parent), WM_MEASUREITEM,
                      (WPARAM)record->id, (LPARAM)&measure);
    if (measure.itemHeight) control->itemHeight = measure.itemHeight;
  }
}

ControlRecord *ui_control(HWND window) {
  const uint32_t handle = hwnd_value(window);
  if (!handle) return NULL;
  for (size_t i = 0; i < UI_CONTROL_LIMIT; ++i)
    if (controls[i].used && controls[i].handle == handle) return &controls[i];
  return NULL;
}

BOOL ui_controls_is_control(uint32_t handle) {
  return ui_control(hwnd_from_value(handle)) != NULL;
}

void ui_controls_forget(uint32_t handle) {
  ControlRecord *control = ui_control(hwnd_from_value(handle));
  ui_scroll_window_forget(handle);
  if (!control) return;
  ui_listbox_release(control);
  control->used = FALSE;
}

void ui_controls_reset(void) {
  for (size_t i = 0; i < UI_CONTROL_LIMIT; ++i)
    if (controls[i].used) ui_listbox_release(&controls[i]);
  memset(controls, 0, sizeof(controls));
  memset(sysBrushes, 0, sizeof(sysBrushes));
  lineHeight = 0;
  ui_focus_window = 0;
  ui_scroll_window_reset();
}

DWORD ui_control_style(HWND window) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  return record ? record->style : 0;
}

void ui_control_notify(HWND window, UINT code) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  if (!record || !record->parent) return;
  (void)SendMessage(hwnd_from_value(record->parent), WM_COMMAND,
                    MAKEWPARAM(record->id, code), (LPARAM)window);
}

DWORD GetSysColor(int index) {
  if (index < 0 || index >= SYS_COLOR_COUNT) return 0;
  return sysColors[index];
}

HBRUSH GetSysColorBrush(int index) {
  if (index < 0 || index >= SYS_COLOR_COUNT) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (!sysBrushes[index]) sysBrushes[index] = CreateSolidBrush(sysColors[index]);
  return sysBrushes[index];
}

void ui_control_fill(HDC dc, const RECT *rect, int color) {
  if (rect->right > rect->left && rect->bottom > rect->top)
    (void)FillRect(dc, rect, GetSysColorBrush(color));
}

void ui_control_frame(HDC dc, const RECT *rect, int color) {
  RECT edge;
  SetRect(&edge, rect->left, rect->top, rect->right, rect->top + 1);
  ui_control_fill(dc, &edge, color);
  SetRect(&edge, rect->left, rect->bottom - 1, rect->right, rect->bottom);
  ui_control_fill(dc, &edge, color);
  SetRect(&edge, rect->left, rect->top, rect->left + 1, rect->bottom);
  ui_control_fill(dc, &edge, color);
  SetRect(&edge, rect->right - 1, rect->top, rect->right, rect->bottom);
  ui_control_fill(dc, &edge, color);
}

void ui_control_edge(HDC dc, const RECT *rect, BOOL raised) {
  const int light = raised ? COLOR_BTNHIGHLIGHT : COLOR_BTNSHADOW;
  const int dark = raised ? COLOR_BTNSHADOW : COLOR_BTNHIGHLIGHT;
  RECT edge;
  SetRect(&edge, rect->left, rect->top, rect->right - 1, rect->top + 1);
  ui_control_fill(dc, &edge, light);
  SetRect(&edge, rect->left, rect->top, rect->left + 1, rect->bottom - 1);
  ui_control_fill(dc, &edge, light);
  SetRect(&edge, rect->left, rect->bottom - 1, rect->right, rect->bottom);
  ui_control_fill(dc, &edge, dark);
  SetRect(&edge, rect->right - 1, rect->top, rect->right, rect->bottom);
  ui_control_fill(dc, &edge, dark);
}

BOOL DrawFocusRect(HDC dc, const RECT *rect) {
  if (!rect) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  // Win32 XORs a dotted frame; without an invert raster operation the
  // frame is drawn solid in the shadow colour.
  ui_control_frame(dc, rect, COLOR_BTNSHADOW);
  return TRUE;
}

HFONT ui_control_select_font(HDC dc, ControlRecord *control) {
  if (!control || !control->font) return NULL;
  return (HFONT)SelectObject(dc, control->font);
}

LRESULT ui_control_text_message(ControlRecord *control, UINT message,
                                WPARAM wParam, LPARAM lParam) {
  const HWND window = hwnd_from_value(control->handle);
  switch (message) {
    case WM_SETTEXT:
      ui_copy_text(control->text, sizeof(control->text), (const char *)lParam);
      (void)InvalidateRect(window, NULL, TRUE);
      return TRUE;
    case WM_GETTEXT:
      if (!wParam || !lParam) return 0;
      ui_copy_text((char *)lParam, (size_t)wParam, control->text);
      return (LRESULT)strlen((const char *)lParam);
    case WM_GETTEXTLENGTH: return (LRESULT)strlen(control->text);
    case WM_SETFONT:
      control->font = (HFONT)wParam;
      if (LOWORD(lParam)) (void)InvalidateRect(window, NULL, TRUE);
      return 0;
    case WM_GETFONT: return (LRESULT)(uintptr_t)control->font;
    case WM_ENABLE: (void)InvalidateRect(window, NULL, TRUE); return 0;
    default: return 0;
  }
}

/* Scroll bar geometry. */

static int span(const ScrollState *bar) {
  int extent = bar->maximum - bar->minimum;
  if (bar->page) extent -= (int)bar->page - 1;
  return extent > 0 ? extent : 0;
}

int ui_scroll_clamp(const ScrollState *bar, int position) {
  if (position < bar->minimum) position = bar->minimum;
  const int highest = bar->minimum + span(bar);
  if (position > highest) position = highest;
  return position;
}

typedef struct ScrollParts {
  LONG start, end, arrow, thumbStart, thumbEnd;
} ScrollParts;

static ScrollParts parts(const RECT *rect, BOOL vertical,
                         const ScrollState *bar) {
  ScrollParts p;
  p.start = vertical ? rect->top : rect->left;
  p.end = vertical ? rect->bottom : rect->right;
  const LONG length = p.end - p.start;
  p.arrow = length < 2 * UI_SCROLL_ARROW ? length / 2 : UI_SCROLL_ARROW;
  const LONG track = length - 2 * p.arrow;
  LONG thumb = UI_SCROLL_ARROW;
  const int range = bar->maximum - bar->minimum + 1;
  if (bar->page && range > 0)
    thumb = (LONG)((int64_t)track * (int64_t)bar->page / range);
  if (thumb < 8) thumb = 8;
  if (thumb > track) thumb = track;
  const int extent = span(bar);
  LONG offset = 0;
  if (extent > 0)
    offset = (LONG)((int64_t)(track - thumb) *
                    (bar->position - bar->minimum) / extent);
  p.thumbStart = p.start + p.arrow + offset;
  p.thumbEnd = p.thumbStart + thumb;
  if (extent <= 0) p.thumbStart = p.thumbEnd = 0;
  return p;
}

static RECT part_rect(const RECT *rect, BOOL vertical, LONG from, LONG to) {
  RECT r = *rect;
  if (vertical) {
    r.top = from;
    r.bottom = to;
  } else {
    r.left = from;
    r.right = to;
  }
  return r;
}

static void arrow_glyph(HDC dc, const RECT *box, BOOL vertical, BOOL forward) {
  const LONG cx = (box->left + box->right) / 2;
  const LONG cy = (box->top + box->bottom) / 2;
  (void)SelectObject(dc, GetStockObject(BLACK_PEN));
  for (LONG i = 0; i < 4; ++i) {
    const LONG d = forward ? i - 2 : 1 - i;
    if (vertical) {
      MoveToEx(dc, cx - i, cy + d, NULL);
      LineTo(dc, cx + i + 1, cy + d);
    } else {
      MoveToEx(dc, cx + d, cy - i, NULL);
      LineTo(dc, cx + d, cy + i + 1);
    }
  }
}

void ui_scroll_draw(HDC dc, const RECT *rect, BOOL vertical,
                    const ScrollState *bar, BOOL enabled) {
  const ScrollParts p = parts(rect, vertical, bar);
  ui_control_fill(dc, rect, COLOR_SCROLLBAR);
  const RECT back = part_rect(rect, vertical, p.start, p.start + p.arrow);
  const RECT next = part_rect(rect, vertical, p.end - p.arrow, p.end);
  ui_control_fill(dc, &back, COLOR_BTNFACE);
  ui_control_edge(dc, &back, TRUE);
  ui_control_fill(dc, &next, COLOR_BTNFACE);
  ui_control_edge(dc, &next, TRUE);
  if (!enabled) return;
  arrow_glyph(dc, &back, vertical, FALSE);
  arrow_glyph(dc, &next, vertical, TRUE);
  if (p.thumbEnd > p.thumbStart) {
    const RECT thumb = part_rect(rect, vertical, p.thumbStart, p.thumbEnd);
    ui_control_fill(dc, &thumb, COLOR_BTNFACE);
    ui_control_edge(dc, &thumb, TRUE);
  }
}

int ui_scroll_hit(const RECT *rect, BOOL vertical, const ScrollState *bar,
                  POINT point, int *thumbPosition) {
  const ScrollParts p = parts(rect, vertical, bar);
  const LONG at = vertical ? point.y : point.x;
  if (at < p.start || at >= p.end) return -1;
  if (at < p.start + p.arrow) return SB_LINEUP;
  if (at >= p.end - p.arrow) return SB_LINEDOWN;
  if (p.thumbEnd <= p.thumbStart) return -1;
  if (at < p.thumbStart) return SB_PAGEUP;
  if (at >= p.thumbEnd) return SB_PAGEDOWN;
  if (thumbPosition) *thumbPosition = bar->position;
  return SB_THUMBPOSITION;
}

BOOL ui_scroll_apply(ScrollState *bar, LPCSCROLLINFO info) {
  if (info->fMask & SIF_RANGE) {
    bar->minimum = info->nMin;
    bar->maximum = info->nMax < info->nMin ? info->nMin : info->nMax;
  }
  if (info->fMask & SIF_PAGE) {
    const int64_t range = (int64_t)bar->maximum - bar->minimum + 1;
    bar->page = (int64_t)info->nPage > range ? (UINT)range : info->nPage;
  }
  if (info->fMask & SIF_POS) bar->position = info->nPos;
  bar->position = ui_scroll_clamp(bar, bar->position);
  return TRUE;
}

void ui_scroll_read(const ScrollState *bar, LPSCROLLINFO info) {
  if (info->fMask & SIF_RANGE) {
    info->nMin = bar->minimum;
    info->nMax = bar->maximum;
  }
  if (info->fMask & SIF_PAGE) info->nPage = bar->page;
  if (info->fMask & SIF_POS) info->nPos = bar->position;
  if (info->fMask & SIF_TRACKPOS) info->nTrackPos = bar->position;
}
