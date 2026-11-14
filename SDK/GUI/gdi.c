#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/win_internal.h"

#define UI_GDI_LIMIT 64
#define UI_GDI_HANDLE_TAG 0x00100000u
#define UI_GDI_STOCK_BASE 0x00007F00u
#define UI_GDI_STOCK_LAST DEFAULT_GUI_FONT

typedef enum GdiKind {
  GDI_NONE = 0,
  GDI_PEN,
  GDI_BRUSH,
  GDI_FONT
} GdiKind;

typedef struct GdiObject {
  GdiKind kind;
  uint8_t generation;
  COLORREF color;
  int style;
  int width;
} GdiObject;

static GdiObject objects[UI_GDI_LIMIT];

void ui_gdi_reset(void) {
  memset(objects, 0, sizeof(objects));
}

static GdiObject stock_object(int id) {
  GdiObject object = {GDI_NONE, 0, RGB(0, 0, 0), PS_SOLID, 1};
  switch (id) {
    case WHITE_BRUSH: object.kind = GDI_BRUSH; object.color = RGB(255, 255, 255); break;
    case LTGRAY_BRUSH: object.kind = GDI_BRUSH; object.color = RGB(192, 192, 192); break;
    case GRAY_BRUSH: object.kind = GDI_BRUSH; object.color = RGB(128, 128, 128); break;
    case DKGRAY_BRUSH: object.kind = GDI_BRUSH; object.color = RGB(64, 64, 64); break;
    case BLACK_BRUSH: object.kind = GDI_BRUSH; break;
    case NULL_BRUSH: object.kind = GDI_BRUSH; object.style = PS_NULL; break;
    case WHITE_PEN: object.kind = GDI_PEN; object.color = RGB(255, 255, 255); break;
    case BLACK_PEN: object.kind = GDI_PEN; break;
    case NULL_PEN: object.kind = GDI_PEN; object.style = PS_NULL; break;
    case OEM_FIXED_FONT:
    case ANSI_FIXED_FONT:
    case ANSI_VAR_FONT:
    case SYSTEM_FONT:
    case DEVICE_DEFAULT_FONT:
    case SYSTEM_FIXED_FONT:
    case DEFAULT_GUI_FONT: object.kind = GDI_FONT; break;
    default: break;
  }
  return object;
}

/* Resolves stock objects, palette brushes and table entries. */
static BOOL resolve(uintptr_t handle, GdiObject *object) {
  if (handle >= 1 && handle <= 16) {
    GdiObject brush = {GDI_BRUSH, 0, UndosPaletteColor((UINT)handle - 1u),
                       PS_SOLID, 1};
    *object = brush;
    return TRUE;
  }
  if (handle >= UI_GDI_STOCK_BASE &&
      handle <= UI_GDI_STOCK_BASE + UI_GDI_STOCK_LAST) {
    *object = stock_object((int)(handle - UI_GDI_STOCK_BASE));
    return object->kind != GDI_NONE;
  }
  if ((handle & 0xFFF00000u) != UI_GDI_HANDLE_TAG) return FALSE;
  const uint32_t index = (uint32_t)(handle & 0xFFu);
  const uint8_t generation = (uint8_t)((handle >> 8) & 0xFFu);
  if (index >= UI_GDI_LIMIT || objects[index].kind == GDI_NONE ||
      objects[index].generation != generation)
    return FALSE;
  *object = objects[index];
  return TRUE;
}

static uintptr_t stock_handle(int id) {
  return UI_GDI_STOCK_BASE + (uintptr_t)id;
}

static uintptr_t allocate(GdiKind kind, COLORREF color, int style, int width) {
  for (uint32_t i = 0; i < UI_GDI_LIMIT; ++i) {
    if (objects[i].kind != GDI_NONE) continue;
    objects[i].kind = kind;
    objects[i].generation = (uint8_t)(objects[i].generation + 1u);
    objects[i].color = color;
    objects[i].style = style;
    objects[i].width = width;
    return UI_GDI_HANDLE_TAG | ((uintptr_t)objects[i].generation << 8) | i;
  }
  ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
  return 0;
}

static DCRecord *gdi_dc(HDC dc) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
  return record;
}

static GdiObject dc_object(const DCRecord *record, GdiKind kind) {
  GdiObject object;
  uintptr_t handle = kind == GDI_PEN ? record->pen : record->brush;
  if (handle && resolve(handle, &object) && object.kind == kind) return object;
  return stock_object(kind == GDI_PEN ? BLACK_PEN : WHITE_BRUSH);
}

/* Draws with color as the backend foreground, then restores the DC color. */
static BOOL fill_with(DCRecord *record, COLORREF color, LONG left, LONG top,
                      LONG right, LONG bottom) {
  if (right < left) { LONG t = left; left = right; right = t; }
  if (bottom < top) { LONG t = top; top = bottom; bottom = t; }
  if (left == right || top == bottom) return TRUE;
  const COLORREF saved = record->textColor;
  record->textColor = color;
  BOOL ok = backend_dc_colors(record) &&
            backend_ok(ui_backend_fill_rectangle(record->handle, left, top,
                                                  right, bottom));
  record->textColor = saved;
  return ok;
}

HGDIOBJ GetStockObject(int id) {
  if (id < 0 || id > UI_GDI_STOCK_LAST || stock_object(id).kind == GDI_NONE) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return (HGDIOBJ)stock_handle(id);
}

HPEN CreatePen(int style, int width, COLORREF color) {
  if (style < PS_SOLID || style > PS_INSIDEFRAME || (color & 0xFF000000u)) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (width < 1) width = 1;
  /* Dashed styles draw solid; the backend has no line patterns. */
  return (HPEN)allocate(GDI_PEN, color, style == PS_NULL ? PS_NULL : PS_SOLID,
                        width);
}

HBRUSH CreateSolidBrush(COLORREF color) {
  if (color & 0xFF000000u) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return (HBRUSH)allocate(GDI_BRUSH, color, PS_SOLID, 1);
}

HGDIOBJ SelectObject(HDC dc, HGDIOBJ handle) {
  DCRecord *record = gdi_dc(dc);
  if (!record) return NULL;
  GdiObject object;
  const uintptr_t value = (uintptr_t)handle;
  if (ui_bitmap_is_handle(value)) return ui_bitmap_select(record, value);
  if (!handle || !resolve(value, &object)) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return NULL;
  }
  uintptr_t *slot = object.kind == GDI_PEN ? &record->pen :
                    object.kind == GDI_BRUSH ? &record->brush : &record->font;
  uintptr_t previous = *slot;
  if (!previous)
    previous = stock_handle(object.kind == GDI_PEN ? BLACK_PEN :
                            object.kind == GDI_BRUSH ? WHITE_BRUSH : SYSTEM_FONT);
  *slot = value;
  return (HGDIOBJ)previous;
}

BOOL DeleteObject(HGDIOBJ handle) {
  const uintptr_t value = (uintptr_t)handle;
  GdiObject object;
  if (ui_bitmap_is_handle(value)) return ui_bitmap_delete(value);
  if (!handle || !resolve(value, &object)) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if ((value & 0xFFF00000u) != UI_GDI_HANDLE_TAG) return TRUE;
  for (size_t i = 0; i < UI_DC_LIMIT; ++i) {
    if (dcs[i].used && (dcs[i].pen == value || dcs[i].brush == value ||
                        dcs[i].font == value)) {
      ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      return FALSE;
    }
  }
  objects[value & 0xFFu].kind = GDI_NONE;
  return TRUE;
}

BOOL MoveToEx(HDC dc, int x, int y, POINT *previous) {
  DCRecord *record = gdi_dc(dc);
  if (!record) return FALSE;
  if (previous) {
    previous->x = record->currentX;
    previous->y = record->currentY;
  }
  record->currentX = x;
  record->currentY = y;
  return TRUE;
}

BOOL LineTo(HDC dc, int x, int y) {
  DCRecord *record = gdi_dc(dc);
  if (!record) return FALSE;
  const GdiObject pen = dc_object(record, GDI_PEN);
  const LONG fromX = record->currentX, fromY = record->currentY;
  BOOL ok = TRUE;
  if (pen.style != PS_NULL) {
    const COLORREF saved = record->textColor;
    record->textColor = pen.color;
    ok = backend_dc_colors(record);
    const LONG dx = x > fromX ? x - fromX : fromX - x;
    const LONG dy = y > fromY ? y - fromY : fromY - y;
    for (int i = 0; ok && i < pen.width; ++i) {
      const LONG offset = i - pen.width / 2;
      const LONG ox = dx >= dy ? 0 : offset;
      const LONG oy = dx >= dy ? offset : 0;
      ok = backend_ok(ui_backend_move_to(record->handle, fromX + ox, fromY + oy)) &&
           backend_ok(ui_backend_line_to(record->handle, x + ox, y + oy));
    }
    record->textColor = saved;
  }
  if (ok) {
    record->currentX = x;
    record->currentY = y;
  }
  return ok;
}

int FillRect(HDC dc, const RECT *rect, HBRUSH brush) {
  DCRecord *record = gdi_dc(dc);
  if (!record) return 0;
  GdiObject object;
  if (!rect) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (!brush || !resolve((uintptr_t)brush, &object) || object.kind != GDI_BRUSH) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return 0;
  }
  if (object.style == PS_NULL) return 1;
  return fill_with(record, object.color, rect->left, rect->top, rect->right,
                   rect->bottom) ? 1 : 0;
}

BOOL PatBlt(HDC dc, int x, int y, int width, int height, DWORD rop) {
  DCRecord *record = gdi_dc(dc);
  if (!record) return FALSE;
  COLORREF color;
  if (rop == BLACKNESS) {
    color = RGB(0, 0, 0);
  } else if (rop == WHITENESS) {
    color = RGB(255, 255, 255);
  } else if (rop == PATCOPY) {
    const GdiObject brush = dc_object(record, GDI_BRUSH);
    if (brush.style == PS_NULL) return TRUE;
    color = brush.color;
  } else {
    /* PATINVERT/DSTINVERT need a read-back or XOR backend operation. */
    ui_core_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
  }
  return fill_with(record, color, x, y, x + width, y + height);
}

static BOOL copy_span(const char *text, size_t length, char *buffer) {
  if (length >= UI_FONT_TEXT_BYTES) {
    ui_core_failure(UI_STATUS_INVALID_SIZE, ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memcpy(buffer, text, length);
  buffer[length] = '\0';
  return utf8_string(buffer, UI_FONT_TEXT_BYTES, NULL, TRUE);
}

static BOOL text_length(LPCSTR text, int count, size_t *length) {
  if (!text && count != 0) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *length = count < 0 ? strlen(text) : (size_t)count;
  return TRUE;
}

static BOOL line_height(DCRecord *record, LONG *height, UiTextMetrics *out) {
  if (!record->hasFont) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  UiTextMetrics metrics;
  if (!backend_ok(ui_backend_get_font_metrics(record->currentFont,
                                              UNDOS_TEXT_REGULAR,
                                              UNDOS_TEXT_BASIC, &metrics)))
    return FALSE;
  *height = (LONG)metrics.lineHeight;
  if (out) *out = metrics;
  return TRUE;
}

static BOOL measure_span(DCRecord *record, const char *text, size_t length,
                         LONG *width) {
  if (!length) {
    *width = 0;
    return TRUE;
  }
  if (!record->hasFont) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  char buffer[UI_FONT_TEXT_BYTES];
  if (!copy_span(text, length, buffer)) return FALSE;
  UiTextExtent extent;
  if (!backend_ok(ui_backend_measure_text(record->currentFont,
                                          UNDOS_TEXT_REGULAR,
                                          UNDOS_TEXT_PROPORTIONAL,
                                          UNDOS_TEXT_BASIC, buffer, &extent)))
    return FALSE;
  *width = (LONG)extent.advanceWidth;
  return TRUE;
}

static BOOL draw_span(DCRecord *record, int x, int y, const char *text,
                      size_t length) {
  if (!length) return TRUE;
  char buffer[UI_FONT_TEXT_BYTES];
  if (!copy_span(text, length, buffer)) return FALSE;
  return backend_dc_colors(record) &&
         backend_ok(ui_backend_draw_text(record->handle, x, y,
                                         UNDOS_TEXT_REGULAR,
                                         UNDOS_TEXT_PROPORTIONAL,
                                         UNDOS_TEXT_BASIC, buffer));
}

BOOL TextOut(HDC dc, int x, int y, LPCSTR text, int count) {
  DCRecord *record = gdi_dc(dc);
  size_t length;
  if (!record || count < 0 || !text_length(text, count, &length)) {
    if (record && count < 0)
      ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return draw_span(record, x, y, text, length);
}

BOOL ExtTextOut(HDC dc, int x, int y, UINT options, const RECT *rect,
                LPCSTR text, UINT count, const int *dx) {
  (void)dx;
  DCRecord *record = gdi_dc(dc);
  if (!record) return FALSE;
  if (options & ~(ETO_OPAQUE | ETO_CLIPPED)) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_FLAGS);
    return FALSE;
  }
  if ((options & ETO_OPAQUE) && rect &&
      !fill_with(record, record->backgroundColor, rect->left, rect->top,
                 rect->right, rect->bottom))
    return FALSE;
  size_t length;
  if (!text_length(text, (int)count, &length)) return FALSE;
  /* ETO_CLIPPED is accepted; the backend clips to the paint region only. */
  return draw_span(record, x, y, text, length);
}

BOOL GetTextExtentPoint32(HDC dc, LPCSTR text, int count, SIZE *size) {
  DCRecord *record = gdi_dc(dc);
  size_t length;
  if (!record) return FALSE;
  if (!size || count < 0 || !text_length(text, count, &length)) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  LONG width, height;
  if (!line_height(record, &height, NULL) ||
      !measure_span(record, text, length, &width))
    return FALSE;
  size->cx = width;
  size->cy = height;
  return TRUE;
}

BOOL GetTextExtentPoint(HDC dc, LPCSTR text, int count, SIZE *size) {
  return GetTextExtentPoint32(dc, text, count, size);
}

BOOL GetTextMetrics(HDC dc, TEXTMETRIC *metrics) {
  DCRecord *record = gdi_dc(dc);
  if (!record) return FALSE;
  if (!metrics) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  UiTextMetrics value;
  LONG height;
  if (!line_height(record, &height, &value)) return FALSE;
  memset(metrics, 0, sizeof(*metrics));
  metrics->tmAscent = (LONG)value.ascent;
  metrics->tmDescent = (LONG)value.descent;
  metrics->tmHeight = (LONG)(value.ascent + value.descent);
  metrics->tmExternalLeading = (LONG)value.lineGap;
  metrics->tmAveCharWidth = (LONG)value.cellWidth;
  metrics->tmMaxCharWidth = (LONG)value.cellWidth;
  metrics->tmWeight = FW_NORMAL;
  metrics->tmDigitizedAspectX = 96;
  metrics->tmDigitizedAspectY = 96;
  metrics->tmFirstChar = 0x20;
  metrics->tmLastChar = 0x7E;
  metrics->tmDefaultChar = '?';
  metrics->tmBreakChar = ' ';
  metrics->tmPitchAndFamily = TMPF_FIXED_PITCH;
  return TRUE;
}

/* Removes '&' mnemonic markers; "&&" becomes a literal '&'. */
static size_t strip_prefix(const char *text, size_t length, char *out) {
  size_t used = 0;
  for (size_t i = 0; i < length; ++i) {
    if (text[i] == '&' && i + 1 < length) ++i;
    else if (text[i] == '&') continue;
    out[used++] = text[i];
  }
  return used;
}

typedef struct DrawLine {
  size_t start;
  size_t length;
  LONG width;
} DrawLine;

#define UI_DRAW_TEXT_LINES 64

static BOOL break_lines(DCRecord *record, const char *text, size_t length,
                        UINT format, LONG limit, DrawLine *lines,
                        size_t *count) {
  size_t used = 0, start = 0;
  while (start <= length && used < UI_DRAW_TEXT_LINES) {
    size_t end = start;
    if (format & DT_SINGLELINE) end = length;
    else while (end < length && text[end] != '\n') ++end;
    size_t stop = end;
    if (stop > start && text[stop - 1] == '\r') --stop;
    LONG width;
    if (!measure_span(record, text + start, stop - start, &width)) return FALSE;
    if ((format & DT_WORDBREAK) && !(format & DT_SINGLELINE) && width > limit) {
      size_t fit = start, scan = start;
      LONG fitWidth = 0;
      while (scan < stop) {
        size_t word = scan;
        while (word < stop && text[word] != ' ') ++word;
        LONG trial;
        if (!measure_span(record, text + start, word - start, &trial)) return FALSE;
        if (trial > limit && fit > start) break;
        fit = word;
        fitWidth = trial;
        scan = word;
        while (scan < stop && text[scan] == ' ') ++scan;
        if (trial > limit) break;
      }
      if (fit < stop) {
        lines[used].start = start;
        lines[used].length = fit - start;
        lines[used].width = fitWidth;
        ++used;
        start = fit;
        while (start < stop && text[start] == ' ') ++start;
        continue;
      }
    }
    lines[used].start = start;
    lines[used].length = stop - start;
    lines[used].width = width;
    ++used;
    if (end >= length) break;
    start = end + 1;
  }
  *count = used;
  return TRUE;
}

int DrawText(HDC dc, LPCSTR text, int count, RECT *rect, UINT format) {
  DCRecord *record = gdi_dc(dc);
  size_t length;
  if (!record) return 0;
  if (!rect || !text_length(text, count, &length)) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  char source[UI_FONT_TEXT_BYTES];
  if (length >= sizeof(source)) {
    ui_core_failure(UI_STATUS_INVALID_SIZE, ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  if (format & DT_NOPREFIX) {
    memcpy(source, text, length);
  } else {
    length = strip_prefix(text, length, source);
  }
  LONG lineHeight;
  if (!line_height(record, &lineHeight, NULL)) return 0;
  DrawLine lines[UI_DRAW_TEXT_LINES];
  size_t lineCount = 0;
  if (!break_lines(record, source, length, format, rect->right - rect->left,
                   lines, &lineCount))
    return 0;
  LONG widest = 0;
  for (size_t i = 0; i < lineCount; ++i)
    if (lines[i].width > widest) widest = lines[i].width;
  const LONG height = (LONG)lineCount * lineHeight;
  if (format & DT_CALCRECT) {
    if (!(format & DT_WORDBREAK) || widest > rect->right - rect->left)
      rect->right = rect->left + widest;
    rect->bottom = rect->top + height;
    return (int)height;
  }
  LONG y = rect->top;
  if (format & DT_SINGLELINE) {
    if (format & DT_VCENTER) y = rect->top + (rect->bottom - rect->top - lineHeight) / 2;
    else if (format & DT_BOTTOM) y = rect->bottom - lineHeight;
  }
  for (size_t i = 0; i < lineCount; ++i, y += lineHeight) {
    LONG x = rect->left;
    if (format & DT_CENTER) x = rect->left + (rect->right - rect->left - lines[i].width) / 2;
    else if (format & DT_RIGHT) x = rect->right - lines[i].width;
    if (!draw_span(record, x, y, source + lines[i].start, lines[i].length))
      return 0;
  }
  return (int)height;
}

BOOL SetRect(RECT *rect, int left, int top, int right, int bottom) {
  if (!rect) return FALSE;
  rect->left = left;
  rect->top = top;
  rect->right = right;
  rect->bottom = bottom;
  return TRUE;
}

BOOL InflateRect(RECT *rect, int dx, int dy) {
  if (!rect) return FALSE;
  rect->left -= dx;
  rect->top -= dy;
  rect->right += dx;
  rect->bottom += dy;
  return TRUE;
}

BOOL OffsetRect(RECT *rect, int dx, int dy) {
  if (!rect) return FALSE;
  rect->left += dx;
  rect->top += dy;
  rect->right += dx;
  rect->bottom += dy;
  return TRUE;
}
