#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/utf8.h"
#include "private/win_internal.h"

static uint32_t close_backend_dc(uint32_t window, uint32_t dc, BOOL windowDC) {
  return windowDC ? ui_backend_release_dc(window, dc) : ui_backend_end_paint(window, dc);
}

/* Shared by BeginPaint and GetDC: a fresh DC record with Win32 defaults and
   the system font selected. On failure the kernel DC is released. */
static BOOL init_window_dc(DCRecord *record, uint32_t windowHandle, uint32_t handle, BOOL windowDC) {
  uint32_t status;
  record->used = TRUE;
  record->ended = FALSE;
  record->handle = handle;
  record->window = windowHandle;
  record->hasFont = FALSE;
  record->hasDefaultFont = FALSE;
  record->textColor = RGB(0, 0, 0);
  record->backgroundColor = UndosPaletteColor(7);
  record->opaque = TRUE;
  record->pen = 0;
  record->brush = 0;
  record->font = 0;
  record->currentX = 0;
  record->currentY = 0;
  record->memory = FALSE;
  record->bitmap = 0;
  record->defaultBitmap = 0;

  UNDOS_FONT font;
  uint32_t generation;
  if (!UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT,
                        UNDOS_GLYPH_FALLBACK_NOTDEF, &font, &generation)) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    (void)close_backend_dc(windowHandle, handle, windowDC);
    record->used = FALSE;
    local_failure(raw, error);
    return FALSE;
  }
  (void)generation;
  uint32_t previous;
  status = ui_backend_select_font(handle, font, &previous);
  if (!backend_ok(status)) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    (void)ui_backend_close_font(font);
    (void)close_backend_dc(windowHandle, handle, windowDC);
    record->used = FALSE;
    local_failure(raw, error);
    return FALSE;
  }
  record->currentFont = font;
  record->hasFont = TRUE;
  record->defaultFont = font;
  record->hasDefaultFont = TRUE;
  record->windowDC = windowDC;
  return TRUE;
}

HDC BeginPaint(HWND window, PAINTSTRUCT *paint) {
  if (!paint) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  uint32_t windowHandle = hwnd_value(window);
  if (!window_by_handle(windowHandle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  DCRecord *record = dc_slot();
  if (!record) {
    local_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  uint32_t handle = 0;
  UiPaintInfo info = {0};
  uint32_t status = ui_backend_begin_paint(windowHandle, &handle, &info);
  if (!backend_ok(status)) return NULL;
  if (!handle) {
    uint32_t cleanup = ui_backend_end_paint(windowHandle, handle);
    (void)cleanup;
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return NULL;
  }
  if (!init_window_dc(record, windowHandle, handle, FALSE)) return NULL;

  PAINTSTRUCT result = {0};
  result.hdc = hdc_from_value(handle);
  result.fErase = info.erased != 0;
  result.rcPaint.left = info.rcPaint.left;
  result.rcPaint.top = info.rcPaint.top;
  result.rcPaint.right = info.rcPaint.right;
  result.rcPaint.bottom = info.rcPaint.bottom;
  *paint = result;
  return result.hdc;
}

BOOL EndPaint(HWND window, const PAINTSTRUCT *paint) {
  if (!paint || !paint->hdc) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  uint32_t handle = hdc_value(paint->hdc);
  DCRecord *record = dc_by_handle(handle);
  if (!record || record->memory || record->windowDC || record->window != hwnd_value(window)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT,
                  record ? ERROR_INVALID_WINDOW_HANDLE : ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (!record->ended) {
    if (!backend_ok(ui_backend_end_paint(record->window, record->handle)))
      return FALSE;
    record->ended = TRUE;
  }
  uint32_t closeStatus = UI_STATUS_OK;
  if (record->hasDefaultFont)
    closeStatus = ui_backend_close_font(record->defaultFont);
  if (closeStatus != UI_STATUS_OK) {
    failure(closeStatus);
    return FALSE;
  }
  record->hasDefaultFont = FALSE;
  record->used = FALSE;
  return TRUE;
}

/* A client-area DC outside WM_PAINT. The kernel clips it to the visible
   client area and presents what was drawn at ReleaseDC. GetDC(NULL) (the
   screen DC) is not supported. */
HDC GetDC(HWND window) {
  uint32_t windowHandle = hwnd_value(window);
  if (!window) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
  }
  if (!window_by_handle(windowHandle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  DCRecord *record = dc_slot();
  if (!record) {
    local_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  uint32_t handle = 0;
  if (!backend_ok(ui_backend_get_dc(windowHandle, &handle))) return NULL;
  if (!init_window_dc(record, windowHandle, handle, TRUE)) return NULL;
  return hdc_from_value(handle);
}

/* Returns 1 when the DC was released, 0 otherwise (Win32). */
int ReleaseDC(HWND window, HDC dc) {
  DCRecord *record = dc ? dc_by_handle(hdc_value(dc)) : NULL;
  if (!record || !record->windowDC || record->window != hwnd_value(window)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return 0;
  }
  if (!backend_ok(ui_backend_release_dc(record->window, record->handle))) return 0;
  if (record->hasDefaultFont) (void)ui_backend_close_font(record->defaultFont);
  record->hasDefaultFont = FALSE;
  record->windowDC = FALSE;
  record->used = FALSE;
  return 1;
}

COLORREF UndosPaletteColor(UINT index) {
  static const uint32_t rgb[16] = {
      0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
      0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
      0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
      0xff5555, 0xff55ff, 0xffff55, 0xffffff};
  if (index > 15) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return CLR_INVALID;
  }
  uint32_t color = rgb[index];
  return RGB((color >> 16) & 255, (color >> 8) & 255, color & 255);
}

uint32_t nearest_palette(COLORREF color) {
  static const uint32_t rgb[16] = {
      0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
      0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
      0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
      0xff5555, 0xff55ff, 0xffff55, 0xffffff};
  const int red = color & 255;
  const int green = (color >> 8) & 255;
  const int blue = (color >> 16) & 255;
  uint32_t best = 0;
  uint32_t bestDistance = UINT32_MAX;
  for (uint32_t i = 0; i < 16; ++i) {
    const int dr = red - (int)((rgb[i] >> 16) & 255);
    const int dg = green - (int)((rgb[i] >> 8) & 255);
    const int db = blue - (int)(rgb[i] & 255);
    const uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
    if (distance < bestDistance) {
      best = i;
      bestDistance = distance;
    }
  }
  return best;
}

BOOL valid_color(COLORREF color) {
  if (color & 0xFF000000u) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return TRUE;
}

COLORREF set_dc_color(HDC handle, COLORREF color, BOOL foreground) {
  DCRecord *record = dc_by_handle(hdc_value(handle));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return CLR_INVALID;
  }
  if (!valid_color(color)) return CLR_INVALID;
  COLORREF previous = foreground ? record->textColor : record->backgroundColor;
  if (foreground) record->textColor = color;
  else record->backgroundColor = color;
  if (!backend_dc_colors(record)) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    if (foreground) record->textColor = previous;
    else record->backgroundColor = previous;
    local_failure(raw, error);
    return CLR_INVALID;
  }
  return previous;
}

COLORREF SetTextColor(HDC dc, COLORREF color) {
  return set_dc_color(dc, color, TRUE);
}

COLORREF SetBkColor(HDC dc, COLORREF color) {
  return set_dc_color(dc, color, FALSE);
}

int SetBkMode(HDC dc, int mode) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return 0;
  }
  if (mode != OPAQUE && mode != TRANSPARENT) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  int previous = record->opaque ? OPAQUE : TRANSPARENT;
  record->opaque = mode == OPAQUE;
  if (!backend_dc_colors(record)) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    record->opaque = previous == OPAQUE;
    local_failure(raw, error);
    return 0;
  }
  return previous;
}

BOOL UndosAcquireFont(enum UndosFontSource source,
                      enum UndosGlyphFallback fallback, UNDOS_FONT *font,
                      uint32_t *generation) {
  if (!font || !generation ||
      (source != UNDOS_FONT_SYSTEM_DEFAULT &&
       source != UNDOS_FONT_BUILTIN_BITMAP) ||
      (fallback != UNDOS_GLYPH_FALLBACK_NOTDEF &&
       fallback != UNDOS_GLYPH_FALLBACK_BITMAP)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  uint32_t value = 0, version = 0;
  uint32_t status = ui_backend_acquire_font(source, fallback, &value, &version);
  if (!backend_ok(status)) return FALSE;
  *font = value;
  *generation = version;
  return TRUE;
}

BOOL UndosCloseFont(UNDOS_FONT font) {
  return backend_ok(ui_backend_close_font(font));
}

BOOL UndosSelectFont(HDC dc, UNDOS_FONT font, UNDOS_FONT *previous,
                     BOOL *hadPrevious) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (!previous || !hadPrevious) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  uint32_t old = 0;
  uint32_t status = ui_backend_select_font(record->handle, font, &old);
  if (!backend_ok(status)) return FALSE;
  const BOOL hadOld = record->hasFont;
  const UNDOS_FONT oldFont = (UNDOS_FONT)old;
  if (record->hasDefaultFont && hadOld &&
      record->defaultFont == oldFont)
    record->hasDefaultFont = FALSE;
  record->currentFont = font;
  record->hasFont = TRUE;
  *previous = oldFont;
  *hadPrevious = hadOld;
  return TRUE;
}

BOOL UndosGetFontMetrics(UNDOS_FONT font, enum UndosTextStyle style,
                         enum UndosTextRepertoire repertoire,
                         UNDOS_TEXT_METRICS *metrics) {
  if (!metrics || (style != UNDOS_TEXT_REGULAR && style != UNDOS_TEXT_BOLD) ||
      (repertoire != UNDOS_TEXT_BASIC && repertoire != UNDOS_TEXT_TERMINAL)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  UiTextMetrics value;
  if (!backend_ok(ui_backend_get_font_metrics(font, style, repertoire, &value)))
    return FALSE;
  UNDOS_TEXT_METRICS result = {
      value.generation, value.ascent, value.descent, value.lineGap,
      value.lineHeight, value.baselineOffset, value.cellWidth, value.cellHeight,
      value.inkLeft, value.inkTop, value.inkRight, value.inkBottom};
  *metrics = result;
  return TRUE;
}

BOOL UndosMeasureText(UNDOS_FONT font, enum UndosTextStyle style,
                      enum UndosTextLayout layout,
                      enum UndosTextRepertoire repertoire, LPCSTR text,
                      UNDOS_TEXT_EXTENT *extent) {
  if (!extent || (style != UNDOS_TEXT_REGULAR && style != UNDOS_TEXT_BOLD) ||
      (layout != UNDOS_TEXT_PROPORTIONAL &&
       layout != UNDOS_TEXT_UNIFORM_GRID) ||
      (repertoire != UNDOS_TEXT_BASIC && repertoire != UNDOS_TEXT_TERMINAL)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!utf8_string(text, UI_FONT_TEXT_BYTES, NULL, TRUE)) return FALSE;
  UiTextExtent value;
  if (!backend_ok(ui_backend_measure_text(font, style, layout, repertoire,
                                          text, &value)))
    return FALSE;
  UNDOS_TEXT_EXTENT result = {
      value.generation, value.advanceWidth, value.lineHeight,
      value.baselineOffset, value.inkLeft, value.inkTop,
      value.inkRight, value.inkBottom};
  *extent = result;
  return TRUE;
}

BOOL UndosDrawText(HDC dc, int x, int y, enum UndosTextStyle style,
                   enum UndosTextLayout layout,
                   enum UndosTextRepertoire repertoire, LPCSTR text) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if ((style != UNDOS_TEXT_REGULAR && style != UNDOS_TEXT_BOLD) ||
      (layout != UNDOS_TEXT_PROPORTIONAL &&
       layout != UNDOS_TEXT_UNIFORM_GRID) ||
      (repertoire != UNDOS_TEXT_BASIC && repertoire != UNDOS_TEXT_TERMINAL)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!utf8_string(text, UI_FONT_TEXT_BYTES, NULL, TRUE)) return FALSE;
  return backend_dc_colors(record) &&
      backend_ok(ui_backend_draw_text(record->handle, x, y, style, layout,
                                      repertoire, text));
}

BOOL UndosFillRectangle(HDC dc, int left, int top, int right, int bottom) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return backend_dc_colors(record) &&
      backend_ok(ui_backend_fill_rectangle(record->handle, left, top,
                                            right, bottom));
}

BOOL UndosMoveTo(HDC dc, int x, int y) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return backend_ok(ui_backend_move_to(record->handle, x, y));
}

BOOL UndosLineTo(HDC dc, int x, int y) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return backend_dc_colors(record) &&
      backend_ok(ui_backend_line_to(record->handle, x, y));
}
