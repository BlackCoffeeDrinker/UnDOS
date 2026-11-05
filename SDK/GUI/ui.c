#include <limits.h>
#include <undoscrt/heap.h>
#include <undoscrt/string.h>
#include <undosgui/extensions.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/utf8.h"

static void ui_result(UNDOS_UI_CONTEXT *context, int32_t result,
                      DWORD operationError, DWORD operationStatus,
                      DWORD cleanupError, DWORD cleanupStatus) {
  ++context->resultSerial;
  context->result = result;
  context->operationError = operationError;
  context->operationStatus = operationStatus;
  context->cleanupError = cleanupError;
  context->cleanupStatus = cleanupStatus;
}

static int ui_context_valid(const UNDOS_UI_CONTEXT *context) {
  return context && context->_sdkState == UI_CONTEXT_MAGIC;
}

static void ui_fail(uint32_t status, DWORD error) {
  ui_core_failure(status, error);
}

static int ui_fail_result(UNDOS_UI_CONTEXT *context, uint32_t status,
                          DWORD error, DWORD cleanupError,
                          DWORD cleanupStatus) {
  ui_fail(status, error);
  ui_result(context, -1, error, status, cleanupError, cleanupStatus);
  return 0;
}

static int ui_refresh_resources(UNDOS_UI_CONTEXT *context,
                                DWORD *operationError,
                                DWORD *operationStatus,
                                DWORD *cleanupError,
                                DWORD *cleanupStatus) {
  if (context->hasDeferredFont) {
    if (!UndosCloseFont(context->deferredFont)) {
      *operationError = GetLastError();
      *operationStatus = UndosGetLastStatus();
      *cleanupError = *operationError;
      *cleanupStatus = *operationStatus;
      return 0;
    }
    context->hasDeferredFont = FALSE;
  }

  UNDOS_FONT candidate = 0;
  uint32_t generation = 0;
  if (!UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT,
                        UNDOS_GLYPH_FALLBACK_NOTDEF, &candidate, &generation)) {
    *operationError = GetLastError();
    *operationStatus = UndosGetLastStatus();
    return 0;
  }
  UNDOS_TEXT_METRICS metrics = {0};
  if (!UndosGetFontMetrics(candidate, UNDOS_TEXT_REGULAR, UNDOS_TEXT_BASIC,
                           &metrics)) {
    *operationError = GetLastError();
    *operationStatus = UndosGetLastStatus();
    if (!UndosCloseFont(candidate)) {
      context->deferredFont = candidate;
      context->hasDeferredFont = TRUE;
      *cleanupError = GetLastError();
      *cleanupStatus = UndosGetLastStatus();
    }
    return 0;
  }
  if (metrics.generation != generation || !metrics.lineHeight) {
    *operationError = ERROR_INVALID_PARAMETER;
    *operationStatus = UI_STATUS_INVALID_ARGUMENT;
    if (!UndosCloseFont(candidate)) {
      context->deferredFont = candidate;
      context->hasDeferredFont = TRUE;
      *cleanupError = GetLastError();
      *cleanupStatus = UndosGetLastStatus();
    }
    return 0;
  }
  if (context->hasFont && !UndosCloseFont(context->font)) {
    *operationError = GetLastError();
    *operationStatus = UndosGetLastStatus();
    context->deferredFont = candidate;
    context->hasDeferredFont = TRUE;
    return 0;
  }
  context->font = candidate;
  context->hasFont = TRUE;
  context->metrics = metrics;
  context->metricsValid = TRUE;
  return 1;
}

BOOL UndosInitUi(UNDOS_UI_CONTEXT *context) {
  if (!context) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (context->_sdkState == UI_CONTEXT_MAGIC &&
      !context->hasFont && !context->hasDeferredFont &&
      !context->hasDisplayDc && !context->_sdkBorrowers &&
      !context->activeDepth) {
    const uint32_t serial = context->resultSerial;
    memset(context, 0, sizeof(*context));
    context->resultSerial = serial;
  } else if (context->_sdkState || context->hasFont ||
             context->hasDeferredFont || context->hasDisplayDc ||
             context->_sdkBorrowers || context->activeDepth) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  context->_sdkState = UI_CONTEXT_MAGIC;
  DWORD operationError = 0, operationStatus = 0;
  DWORD cleanupError = 0, cleanupStatus = 0;
  if (!ui_refresh_resources(context, &operationError, &operationStatus,
                            &cleanupError, &cleanupStatus)) {
    ui_result(context, -1, operationError, operationStatus,
              cleanupError, cleanupStatus);
    ui_fail(operationStatus, operationError);
    return FALSE;
  }
  ui_result(context, 1, 0, 0, 0, 0);
  return TRUE;
}

BOOL UndosRefreshUi(UNDOS_UI_CONTEXT *context) {
  if (!ui_context_valid(context)) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (context->activeDepth >= 2) {
    return (BOOL)ui_fail_result(context, UI_STATUS_RETRY, ERROR_RETRY, 0, 0);
  }
  ++context->activeDepth;
  DWORD operationError = 0, operationStatus = 0;
  DWORD cleanupError = 0, cleanupStatus = 0;
  const int okay = ui_refresh_resources(context, &operationError,
      &operationStatus, &cleanupError, &cleanupStatus);
  --context->activeDepth;
  if (!okay) {
    if (!operationStatus) {
      operationError = GetLastError();
      operationStatus = UndosGetLastStatus();
    }
    ui_result(context, -1, operationError, operationStatus,
              cleanupError, cleanupStatus);
    ui_fail(operationStatus, operationError);
    return FALSE;
  }
  ui_result(context, 1, 0, 0, 0, 0);
  return TRUE;
}

BOOL UndosCloseUi(UNDOS_UI_CONTEXT *context) {
  if (!ui_context_valid(context)) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (context->activeDepth || context->_sdkBorrowers) {
    return (BOOL)ui_fail_result(context, UI_STATUS_RETRY, ERROR_RETRY, 0, 0);
  }
  ++context->activeDepth;
  DWORD error = 0, status = 0;
  if (context->hasFont) {
    if (UndosCloseFont(context->font)) {
      context->hasFont = FALSE;
      context->metricsValid = FALSE;
    } else {
      error = GetLastError();
      status = UndosGetLastStatus();
    }
  }
  if (context->hasDeferredFont) {
    if (UndosCloseFont(context->deferredFont)) {
      context->hasDeferredFont = FALSE;
    } else if (!status) {
      error = GetLastError();
      status = UndosGetLastStatus();
    }
  }
  if (context->hasDisplayDc) {
    const uint32_t dcStatus =
        ui_backend_delete_dc((uint32_t)(uintptr_t)context->displayDc);
    if (dcStatus == UI_STATUS_OK) {
      context->hasDisplayDc = FALSE;
      context->displayDcStale = FALSE;
    } else if (!status) {
      status = dcStatus;
      error = ui_core_error_from_status(dcStatus);
    }
  }
  --context->activeDepth;
  if (status) {
    ui_result(context, -1, error, status, 0, 0);
    ui_fail(status, error);
    return FALSE;
  }
  context->metricsValid = FALSE;
  ui_result(context, 1, 0, 0, 0, 0);
  return TRUE;
}

BOOL UndosGetDisplaySize(UNDOS_UI_CONTEXT *context, UINT *width,
                         UINT *height) {
  if (!ui_context_valid(context) || !width || !height) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (context->displayDcStale && context->hasDisplayDc) {
    uint32_t status = ui_backend_delete_dc(
        (uint32_t)(uintptr_t)context->displayDc);
    if (status != UI_STATUS_OK) {
      ui_fail(status, ui_core_error_from_status(status));
      return FALSE;
    }
    context->hasDisplayDc = FALSE;
    context->displayDcStale = FALSE;
  }
  if (!context->hasDisplayDc) {
    uint32_t dc = 0;
    uint32_t status = ui_backend_create_display_dc(&dc);
    if (status != UI_STATUS_OK) {
      ui_fail(status, ui_core_error_from_status(status));
      return FALSE;
    }
    context->displayDc = (HDC)(uintptr_t)dc;
    context->hasDisplayDc = TRUE;
  }
  uint32_t resultWidth = 0, resultHeight = 0;
  uint32_t status = ui_backend_get_display_size(
      (uint32_t)(uintptr_t)context->displayDc, &resultWidth, &resultHeight);
  if (status != UI_STATUS_OK) {
    ui_fail(status, ui_core_error_from_status(status));
    return FALSE;
  }
  if (!resultWidth || !resultHeight || resultWidth > INT_MAX ||
      resultHeight > INT_MAX) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *width = resultWidth;
  *height = resultHeight;
  return TRUE;
}

BOOL UndosAttachUiContext(HWND window, UNDOS_UI_CONTEXT *context);

static int text_length(LPCSTR text, UINT *length) {
  if (!text || (uintptr_t)text <= UINT16_MAX) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  UINT n = 0;
  while (n <= 1024 && text[n]) ++n;
  if (n > 1024) {
    ui_fail(UI_STATUS_INVALID_SIZE, ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  if (!undos_utf8_valid(text, n)) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_NO_UNICODE_TRANSLATION);
    return 0;
  }
  *length = n;
  return 1;
}

static int ranges_overlap(const void *a, size_t an, const void *b, size_t bn) {
  const uintptr_t ap = (uintptr_t)a, bp = (uintptr_t)b;
  if (an > UINTPTR_MAX - ap || bn > UINTPTR_MAX - bp) return 1;
  return ap < bp + bn && bp < ap + an;
}

static BOOL fit_text(UNDOS_FONT font, enum UndosTextStyle style,
                     enum UndosTextLayout layout,
                     enum UndosTextRepertoire repertoire, LPCSTR text,
                     LPSTR output, UINT capacity, UINT pixelWidth,
                     UNDOS_TEXT_EXTENT *extent, BOOL tail) {
  UINT length = 0;
  if (!output || !extent || capacity > 1025 ||
      capacity < (tail ? 1u : 4u)) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!text_length(text, &length)) return FALSE;
  if (ranges_overlap(text, (size_t)length + 1, output, capacity)) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  UNDOS_TEXT_EXTENT original = {0};
  if (!UndosMeasureText(font, style, layout, repertoire, text, &original))
    return FALSE;
  char candidate[1025];
  if (length < capacity && original.advanceWidth <= pixelWidth) {
    memcpy(candidate, text, (size_t)length + 1);
    memcpy(output, candidate, (size_t)length + 1);
    *extent = original;
    return TRUE;
  }
  UINT boundaries[1025];
  UINT count = 0;
  for (UINT i = 1; i <= length; ++i) {
    const unsigned char byte = (unsigned char)text[i];
    if (i == length || (byte & 0xC0u) != 0x80u)
      boundaries[count++] = i;
  }
  UINT best = 0;
  UNDOS_TEXT_EXTENT bestExtent = {0};
  if (!tail) {
    UNDOS_TEXT_EXTENT dotsExtent = {0};
    if (!UndosMeasureText(font, style, layout, repertoire, "...", &dotsExtent))
      return FALSE;
    if (dotsExtent.advanceWidth > pixelWidth) {
      UNDOS_TEXT_EXTENT emptyExtent = {0};
      if (!UndosMeasureText(font, style, layout, repertoire, "", &emptyExtent))
        return FALSE;
      emptyExtent.advanceWidth = 0;
      memcpy(output, "", 1);
      *extent = emptyExtent;
      return TRUE;
    }
  }
  for (UINT scalars = count + 1; scalars > 0; --scalars) {
    const UINT selected = scalars - 1;
    const UINT start = tail && selected ? boundaries[count - selected] : 0;
    const UINT bytes = tail ? length - start
                            : (selected ? boundaries[selected - 1] : 0);
    const UINT suffix = tail || selected == count ? 0u : 3u;
    if ((uint64_t)bytes + suffix + 1 > capacity ||
        bytes + suffix > 1024)
      continue;
    memcpy(candidate, text + start, bytes);
    if (suffix) memcpy(candidate + bytes, "...", suffix);
    candidate[bytes + suffix] = 0;
    UNDOS_TEXT_EXTENT measured = {0};
    if (!UndosMeasureText(font, style, layout, repertoire, candidate,
                          &measured))
      return FALSE;
    if (measured.advanceWidth <= pixelWidth) {
      best = bytes + suffix;
      bestExtent = measured;
      break;
    }
  }
  memcpy(output, candidate, best + 1);
  *extent = bestExtent;
  return TRUE;
}

BOOL UndosFitTextPrefix(UNDOS_FONT font, enum UndosTextStyle style,
                        enum UndosTextLayout layout,
                        enum UndosTextRepertoire repertoire, LPCSTR text,
                        LPSTR output, UINT capacity, UINT pixelWidth,
                        UNDOS_TEXT_EXTENT *extent) {
  return fit_text(font, style, layout, repertoire, text, output, capacity,
                  pixelWidth, extent, FALSE);
}

BOOL UndosFitTextTail(UNDOS_FONT font, enum UndosTextStyle style,
                      enum UndosTextLayout layout,
                      enum UndosTextRepertoire repertoire, LPCSTR text,
                      LPSTR output, UINT capacity, UINT pixelWidth,
                      UNDOS_TEXT_EXTENT *extent) {
  return fit_text(font, style, layout, repertoire, text, output, capacity,
                  pixelWidth, extent, TRUE);
}

static BOOL fill_color(HDC dc, COLORREF color, int left, int top,
                       int right, int bottom) {
  if (right <= left || bottom <= top) return TRUE;
  if (SetTextColor(dc, color) == CLR_INVALID ||
      SetBkColor(dc, color) == CLR_INVALID ||
      SetBkMode(dc, OPAQUE) == 0)
    return FALSE;
  return UndosFillRectangle(dc, left, top, right, bottom);
}

BOOL UndosGetButtonHeight(UNDOS_FONT font, LPCSTR label, int *height) {
  if (!height || !label) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  UNDOS_TEXT_EXTENT extent = {0};
  if (!UndosMeasureText(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
                        UNDOS_TEXT_BASIC, label, &extent))
    return FALSE;
  if (extent.lineHeight > INT_MAX - 8) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *height = (int)extent.lineHeight + 8;
  return TRUE;
}

BOOL UndosDrawButton(HDC dc, UNDOS_FONT font, int x, int y, int width,
                     LPCSTR label, BOOL focused) {
  if (width <= 0 || !label || (focused != FALSE && focused != TRUE) ||
      !ui_core_dc_uses_font(dc, font))
    return FALSE;
  int height = 0;
  if (!UndosGetButtonHeight(font, label, &height)) return FALSE;
  const COLORREF ink = UndosPaletteColor(0);
  const COLORREF face = UndosPaletteColor(7);
  const COLORREF paper = UndosPaletteColor(15);
  const COLORREF shade = UndosPaletteColor(8);
  if (ink == CLR_INVALID || face == CLR_INVALID || paper == CLR_INVALID ||
      shade == CLR_INVALID ||
      !fill_color(dc, focused ? ink : shade, x, y, x + width, y + height) ||
      !fill_color(dc, paper, x + 1, y + 1, x + width - 1, y + height - 1) ||
      !fill_color(dc, face, x + 3, y + 3, x + width - 2, y + height - 2))
    return FALSE;
  char fitted[1025] = {0};
  UNDOS_TEXT_EXTENT extent = {0};
  if (!UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, label, fitted, sizeof(fitted),
      width > 8 ? (UINT)(width - 8) : 0, &extent))
    return FALSE;
  if (SetTextColor(dc, ink) == CLR_INVALID ||
      SetBkColor(dc, face) == CLR_INVALID || SetBkMode(dc, OPAQUE) == 0)
    return FALSE;
  return UndosDrawText(dc, x + (width - (int)extent.advanceWidth) / 2,
      y + (height - (int)extent.lineHeight) / 2, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, fitted);
}

BOOL UndosDrawClassicIcon(HDC dc, int x, int y, UINT kind, BOOL selected) {
  (void)selected;
  const UINT shape = kind % 3;
  const COLORREF ink = UndosPaletteColor(0);
  const COLORREF face = UndosPaletteColor(7);
  const COLORREF paper = UndosPaletteColor(15);
  const COLORREF accent = UndosPaletteColor(shape == 0 ? 1 : shape == 1 ? 4 : 2);
  const COLORREF highlight = UndosPaletteColor(1);
#define F(l,t,r,b,c) do { if (!fill_color(dc, (c), x+(l), y+(t), x+(r), y+(b))) return FALSE; } while (0)
  if (shape == 0) {
    F(1,1,31,25,ink); F(2,2,30,24,face); F(4,4,28,21,accent);
    F(7,8,10,9,paper); F(9,9,11,10,paper); F(7,10,10,11,paper);
    F(13,13,20,14,paper); F(12,25,20,28,ink); F(5,28,27,31,ink);
    F(6,28,26,29,face);
  } else if (shape == 1) {
    F(4,1,28,32,ink); F(5,2,27,31,paper); F(5,2,27,7,accent);
    for (int line = 0; line < 4; ++line)
      F(8,11 + line * 4,23 - (line % 2) * 4,12 + line * 4,highlight);
  } else {
    F(0,3,32,29,ink); F(1,4,31,28,face); F(2,5,30,10,accent);
    F(3,12,29,26,paper); F(5,15,9,17,ink); F(7,17,11,19,ink);
    F(5,19,9,21,ink); F(14,21,22,23,ink);
  }
#undef F
  return TRUE;
}

typedef struct UiDialogState {
  BOOL active;
  BOOL ownsDepth;
  BOOL done;
  BOOL accepted;
  BOOL failed;
  BOOL confirm;
  BOOL shiftDown;
  BOOL closeOwner;
  HWND owner;
  HWND window;
  UNDOS_UI_CONTEXT *context;
  UINT count;
  UINT focus;
  char *message;
  char *title;
  char *labels[4];
  char *values[4];
  LPSTR destinations[4];
  UINT capacities[4];
  uint32_t screenWidth;
  uint32_t screenHeight;
  int width;
  int height;
  int x;
  int y;
  int logicalHeight;
  int lineHeight;
  int fieldHeight;
  int fieldTop;
  int rowPitch;
  int buttonY;
  int buttonHeight;
  int messageLines;
  int scrollY;
  int viewportHeight;
  const char *inputError;
  char *storage;
  PAINTSTRUCT paint;
  BOOL paintActive;
  UNDOS_FONT previousFont;
  BOOL hasPreviousFont;
  DWORD operationError;
  DWORD operationStatus;
  DWORD cleanupError;
  DWORD cleanupStatus;
} UiDialogState;

static UiDialogState dialogState;
static BOOL dialogClassRegistered;

static UINT utf8_character_bytes(unsigned char lead) {
  if (lead < 0x80) return 1;
  if ((lead & 0xE0u) == 0xC0u) return 2;
  if ((lead & 0xF0u) == 0xE0u) return 3;
  if ((lead & 0xF8u) == 0xF0u) return 4;
  return 0;
}

static UINT dialog_next_line(const char *text, UINT available,
                             UINT *consumed, char *line) {
  UINT offset = 0, bytes = 0;
  if (!text[offset]) {
    *consumed = 0;
    line[0] = 0;
    return 0;
  }
  while (text[offset] && text[offset] != '\n') {
    const UINT characterBytes = utf8_character_bytes(
        (unsigned char)text[offset]);
    if (!characterBytes || offset + characterBytes > 4096) break;
    if (offset + characterBytes >= 1024) break;
    memcpy(line + bytes, text + offset, characterBytes);
    line[bytes + characterBytes] = 0;
    UNDOS_TEXT_EXTENT extent = {0};
    if (!UndosMeasureText(dialogState.context->font, UNDOS_TEXT_REGULAR,
        UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, line, &extent) ||
        extent.advanceWidth > available) {
      if (bytes) {
        line[bytes] = 0;
        break;
      }
      bytes = characterBytes;
      offset += characterBytes;
      break;
    }
    bytes += characterBytes;
    offset += characterBytes;
  }
  line[bytes] = 0;
  if (text[offset] == '\n') ++offset;
  if (!bytes && !offset && text[0]) offset = 1;
  *consumed = offset;
  return bytes;
}

static int dialog_count_lines(const char *text, UINT width) {
  int lines = 0;
  UINT offset = 0;
  char line[1025];
  while (text[offset]) {
    UINT consumed = 0;
    (void)dialog_next_line(text + offset, width, &consumed, line);
    if (!consumed) break;
    offset += consumed;
    ++lines;
  }
  return lines;
}

static int dialog_layout(UiDialogState *state) {
  UNDOS_UI_CONTEXT *context = state->context;
  if (!context->metricsValid || !context->metrics.lineHeight ||
      context->metrics.lineHeight > INT_MAX - 8)
    return 0;
  state->lineHeight = (int)context->metrics.lineHeight;
  state->fieldHeight = state->lineHeight + 8;
  UINT contentWidth = 0;
  for (UINT i = 0; i < state->count; ++i) {
    UNDOS_TEXT_EXTENT extent = {0};
    if (!UndosMeasureText(context->font, UNDOS_TEXT_REGULAR,
        UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, state->labels[i], &extent) ||
        extent.advanceWidth > INT_MAX)
      return 0;
    if (extent.advanceWidth > contentWidth)
      contentWidth = extent.advanceWidth;
  }
  UINT textWidth = state->screenWidth > 40 ? state->screenWidth - 40 : 1;
  if (textWidth > 408) textWidth = 408;
  state->messageLines = dialog_count_lines(state->message, textWidth);
  const uint64_t logicalWidth = (uint64_t)contentWidth + 32;
  int width = logicalWidth < 432 ? 432 :
      logicalWidth > 440 ? 440 : (int)logicalWidth;
  if (state->screenWidth < 32) return 0;
  if ((uint32_t)width > state->screenWidth - 16)
    width = (int)(state->screenWidth - 16);
  if (width <= 0) return 0;
  state->width = width;
  state->fieldTop = 10 + state->messageLines * state->lineHeight +
      (state->messageLines ? 8 : 0);
  state->rowPitch = state->lineHeight * 2 + 14;
  state->buttonHeight = 0;
  if (!UndosGetButtonHeight(context->font, "OK", &state->buttonHeight))
    return 0;
  if (state->confirm) {
    int cancelHeight = 0;
    if (!UndosGetButtonHeight(context->font, "Cancel", &cancelHeight))
      return 0;
    if (cancelHeight > state->buttonHeight)
      state->buttonHeight = cancelHeight;
  }
  state->buttonY = state->fieldTop + (int)state->count * state->rowPitch;
  const uint64_t height = (uint64_t)state->buttonY +
      state->buttonHeight + state->lineHeight + 18;
  if (height > INT_MAX) return 0;
  state->logicalHeight = (int)height;
  const int maximum = state->screenHeight > 48
      ? (int)state->screenHeight - 48 : (int)state->screenHeight;
  state->height = state->logicalHeight < maximum
      ? state->logicalHeight : maximum;
  state->viewportHeight = state->height > 28 ? state->height - 28 : state->height;
  return state->height > 0;
}

static void dialog_reveal(UiDialogState *state) {
  int top = state->buttonY, bottom = state->buttonY + state->buttonHeight;
  if (state->focus < state->count) {
    top = state->fieldTop + (int)state->focus * state->rowPitch;
    bottom = top + state->rowPitch;
  }
  if (top < state->scrollY) state->scrollY = top;
  else if (bottom > state->scrollY + state->viewportHeight)
    state->scrollY = bottom - state->viewportHeight;
  const int maxScroll = state->logicalHeight > state->viewportHeight
      ? state->logicalHeight - state->viewportHeight : 0;
  if (state->scrollY < 0) state->scrollY = 0;
  if (state->scrollY > maxScroll) state->scrollY = maxScroll;
}

static void dialog_error(UiDialogState *state, DWORD error, DWORD status) {
  if (state->operationStatus) return;
  state->operationError = error;
  state->operationStatus = status;
  state->failed = TRUE;
  state->done = TRUE;
}

static void dialog_cleanup_error(UiDialogState *state) {
  state->failed = TRUE;
  state->done = TRUE;
}

static BOOL dialog_finish_paint(UiDialogState *state) {
  if (state->paintActive) {
    if (!EndPaint(state->window, &state->paint)) {
      if (!state->cleanupStatus) {
        state->cleanupError = GetLastError();
        state->cleanupStatus = UndosGetLastStatus();
      }
      return FALSE;
    }
    state->paintActive = FALSE;
  }
  if (state->hasPreviousFont) {
    if (!UndosCloseFont(state->previousFont)) {
      if (!state->cleanupStatus) {
        state->cleanupError = GetLastError();
        state->cleanupStatus = UndosGetLastStatus();
      }
      if (ui_core_retain_font(state->window, state->previousFont))
        state->hasPreviousFont = FALSE;
      return FALSE;
    }
    state->hasPreviousFont = FALSE;
  }
  return TRUE;
}

static BOOL dialog_set_colors(HDC dc, UINT fg, UINT bg) {
  return SetTextColor(dc, UndosPaletteColor(fg)) != CLR_INVALID &&
      SetBkColor(dc, UndosPaletteColor(bg)) != CLR_INVALID &&
      SetBkMode(dc, OPAQUE) != 0;
}

static BOOL dialog_draw_text(HDC dc, int x, int y, LPCSTR text,
                             UINT foreground, UINT background) {
  return dialog_set_colors(dc, foreground, background) &&
      UndosDrawText(dc, x, y, UNDOS_TEXT_REGULAR,
          UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, text);
}

static void dialog_paint(UiDialogState *state) {
  if (!dialog_finish_paint(state)) {
    dialog_cleanup_error(state);
    return;
  }
  memset(&state->paint, 0, sizeof(state->paint));
  state->paint.hdc = BeginPaint(state->window, &state->paint);
  if (!state->paint.hdc) {
    dialog_error(state, GetLastError(), UndosGetLastStatus());
    return;
  }
  state->paintActive = TRUE;
  BOOL hadPrevious = FALSE;
  if (!UndosSelectFont(state->paint.hdc, state->context->font,
                       &state->previousFont, &hadPrevious)) {
    dialog_error(state, GetLastError(), UndosGetLastStatus());
    (void)dialog_finish_paint(state);
    return;
  }
  state->hasPreviousFont = hadPrevious;
  if (hadPrevious && !UndosCloseFont(state->previousFont)) {
    state->cleanupError = GetLastError();
    state->cleanupStatus = UndosGetLastStatus();
    if (ui_core_retain_font(state->window, state->previousFont))
      state->hasPreviousFont = FALSE;
    dialog_cleanup_error(state);
    if (!EndPaint(state->window, &state->paint) && !state->cleanupStatus) {
      state->cleanupError = GetLastError();
      state->cleanupStatus = UndosGetLastStatus();
    } else {
      state->paintActive = FALSE;
    }
    return;
  }
  state->hasPreviousFont = FALSE;
  HDC dc = state->paint.hdc;
  const int originY = 10 - state->scrollY;
  UINT offset = 0;
  char line[1025];
  for (int row = 0; row < state->messageLines && state->message[offset]; ++row) {
    UINT consumed = 0;
    (void)dialog_next_line(state->message + offset,
        state->width > 24 ? (UINT)(state->width - 24) : 1, &consumed, line);
    if (!consumed) break;
    if (!dialog_draw_text(dc, 12, originY + row * state->lineHeight,
                          line, 0, 7)) {
      dialog_error(state, GetLastError(), UndosGetLastStatus());
      break;
    }
    offset += consumed;
  }
  for (UINT i = 0; !state->failed && i < state->count; ++i) {
    const int rowY = state->fieldTop + (int)i * state->rowPitch -
                     state->scrollY;
    if (rowY + state->rowPitch < 0 || rowY > state->height) continue;
    char label[1025] = {0};
    UNDOS_TEXT_EXTENT extent = {0};
    if (!UndosFitTextPrefix(state->context->font, UNDOS_TEXT_REGULAR,
        UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, state->labels[i],
        label, sizeof(label), (UINT)(state->width - 24), &extent) ||
        !dialog_draw_text(dc, 12, rowY, label, 0, 7)) {
      dialog_error(state, GetLastError(), UndosGetLastStatus());
      break;
    }
    const int fieldTop = rowY + state->lineHeight + 4;
    const UINT fieldBg = state->focus == i ? 1u : 8u;
    if (!fill_color(dc, UndosPaletteColor(fieldBg), 10, fieldTop,
                    state->width - 10, fieldTop + state->fieldHeight) ||
        !fill_color(dc, UndosPaletteColor(15), 12, fieldTop + 2,
                    state->width - 12, fieldTop + state->fieldHeight - 2)) {
      dialog_error(state, GetLastError(), UndosGetLastStatus());
      break;
    }
    char fitted[1025] = {0};
    if (!UndosFitTextTail(state->context->font, UNDOS_TEXT_REGULAR,
        UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, state->values[i], fitted,
        sizeof(fitted), (UINT)(state->width - 32), &extent) ||
        !dialog_draw_text(dc, 14, fieldTop + 4, fitted, 0, 15)) {
      dialog_error(state, GetLastError(), UndosGetLastStatus());
      break;
    }
  }
  if (!state->failed) {
    const int y = state->buttonY - state->scrollY;
    if (!UndosDrawButton(dc, state->context->font, 120, y, 80, "OK",
                         state->focus == state->count) ||
        (state->confirm && !UndosDrawButton(dc, state->context->font,
          224, y, 80, "Cancel", state->focus == state->count + 1))) {
      dialog_error(state, GetLastError(), UndosGetLastStatus());
    }
    if (state->inputError)
      (void)dialog_draw_text(dc, 12, y + state->buttonHeight + 4,
                             state->inputError, 4, 7);
  }
  if (!dialog_finish_paint(state) && !state->operationStatus)
    dialog_cleanup_error(state);
}

static BOOL dialog_keyboard(UINT message, WPARAM key, LPARAM flags) {
  UiDialogState *state = &dialogState;
  if (message == WM_KEYUP && (key == VK_SHIFT || key == VK_LSHIFT ||
                              key == VK_RSHIFT)) {
    state->shiftDown = FALSE;
    return TRUE;
  }
  if (message == WM_KEYDOWN && (key == VK_SHIFT || key == VK_LSHIFT ||
                                key == VK_RSHIFT)) {
    state->shiftDown = TRUE;
    return TRUE;
  }
  if (message == WM_KEYDOWN) {
    if (key == VK_ESCAPE) {
      state->done = TRUE;
    } else if (key == VK_TAB) {
      const UINT controls = state->count + (state->confirm ? 2u : 1u);
      state->focus = (state->focus + (state->shiftDown ? controls - 1 : 1)) %
                     controls;
      dialog_reveal(state);
      (void)InvalidateRect(state->window, NULL, TRUE);
    } else if (key == VK_RETURN ||
               (key == VK_SPACE && state->focus >= state->count)) {
      state->accepted = state->focus != state->count + 1;
      state->done = TRUE;
    } else if (key == VK_PRIOR || key == VK_NEXT || key == VK_HOME ||
               key == VK_END) {
      const int page = state->viewportHeight > state->lineHeight
          ? state->viewportHeight - state->lineHeight : 1;
      if (key == VK_HOME) state->scrollY = 0;
      else if (key == VK_END) state->scrollY = state->logicalHeight;
      else state->scrollY += key == VK_NEXT ? page : -page;
      dialog_reveal(state);
      (void)InvalidateRect(state->window, NULL, TRUE);
    }
    (void)flags;
    return TRUE;
  }
  if (message != WM_CHAR || state->focus >= state->count) return FALSE;
  char *value = state->values[state->focus];
  const UINT capacity = state->capacities[state->focus];
  UINT bytes = (UINT)strlen(value);
  state->inputError = NULL;
  if (key == 8) {
    if (bytes) {
      do { --bytes; } while (bytes && ((unsigned char)value[bytes] & 0xC0u) == 0x80u);
      value[bytes] = 0;
    }
  } else if (key >= 0x20 && key != 0x7F && key <= 0x10FFFF &&
             !(key >= 0xD800 && key <= 0xDFFF)) {
    char encoded[4];
    UINT count = 0;
    if (key <= 0x7F) encoded[count++] = (char)key;
    else if (key <= 0x7FF) {
      encoded[count++] = (char)(0xC0 | (key >> 6));
      encoded[count++] = (char)(0x80 | (key & 0x3F));
    } else if (key <= 0xFFFF) {
      encoded[count++] = (char)(0xE0 | (key >> 12));
      encoded[count++] = (char)(0x80 | ((key >> 6) & 0x3F));
      encoded[count++] = (char)(0x80 | (key & 0x3F));
    } else {
      encoded[count++] = (char)(0xF0 | (key >> 18));
      encoded[count++] = (char)(0x80 | ((key >> 12) & 0x3F));
      encoded[count++] = (char)(0x80 | ((key >> 6) & 0x3F));
      encoded[count++] = (char)(0x80 | (key & 0x3F));
    }
    if ((uint64_t)bytes + count < capacity) {
      memcpy(value + bytes, encoded, count);
      value[bytes + count] = 0;
    } else {
      state->inputError = "Field is full";
    }
  } else {
    state->inputError = "Invalid text";
  }
  (void)InvalidateRect(state->window, NULL, TRUE);
  return TRUE;
}

static LRESULT CALLBACK dialog_proc(HWND window, UINT message, WPARAM wParam,
                                    LPARAM lParam) {
  UiDialogState *state = &dialogState;
  if (!state->active || window != state->window)
    return DefWindowProc(window, message, wParam, lParam);
  if (message == WM_PAINT) {
    dialog_paint(state);
  } else if (message == WM_CLOSE || message == WM_DESTROY) {
    state->done = TRUE;
  } else if (message == WM_FONTCHANGED) {
    if (UndosRefreshUi(state->context) && dialog_layout(state)) {
      (void)InvalidateRect(window, NULL, TRUE);
    } else {
      dialog_error(state, state->context->operationError,
                   state->context->operationStatus);
    }
  } else if (message == WM_SIZE) {
    dialog_reveal(state);
  } else if (message == WM_KEYDOWN || message == WM_KEYUP ||
             message == WM_CHAR) {
    (void)dialog_keyboard(message, wParam, lParam);
  } else if (message == WM_LBUTTONDOWN) {
    const int x = (int16_t)(lParam & 0xFFFF);
    const int y = (int16_t)(lParam >> 16) + state->scrollY;
    for (UINT i = 0; i < state->count; ++i) {
      const int top = state->fieldTop + (int)i * state->rowPitch;
      if (x >= 10 && x < state->width - 10 && y >= top &&
          y < top + state->rowPitch - 4)
        state->focus = i;
    }
    if (y >= state->buttonY &&
        y < state->buttonY + state->buttonHeight) {
      if (x >= 120 && x < 200) {
        state->accepted = TRUE;
        state->done = TRUE;
      } else if (state->confirm && x >= 224 && x < 304) {
        state->accepted = FALSE;
        state->done = TRUE;
      }
    }
    dialog_reveal(state);
    (void)InvalidateRect(window, NULL, TRUE);
  } else if (message == WM_DISPLAYCHANGE) {
    if (!UndosGetDisplaySize(state->context, &state->screenWidth,
                             &state->screenHeight) ||
        !dialog_layout(state)) {
      dialog_error(state, GetLastError(), UndosGetLastStatus());
    } else {
      state->x = (int)(state->screenWidth - (UINT)state->width) / 2;
      state->y = (int)(state->screenHeight - (UINT)state->height) / 2;
      if (state->x < 0) state->x = 0;
      if (state->y < 0) state->y = 0;
      if (!MoveWindow(window, state->x, state->y, state->width,
                      state->height, TRUE)) {
        dialog_error(state, GetLastError(), UndosGetLastStatus());
      } else {
        dialog_reveal(state);
        (void)InvalidateRect(window, NULL, TRUE);
      }
    }
  } else {
    return DefWindowProc(window, message, wParam, lParam);
  }
  return 0;
}

static BOOL ensure_dialog_class(void) {
  if (dialogClassRegistered) return TRUE;
  WNDCLASS klass = {0};
  klass.lpfnWndProc = dialog_proc;
  klass.hbrBackground = UNDOS_PALETTE_BRUSH(7);
  klass.lpszClassName = "UndosUiDialog";
  if (!RegisterClass(&klass)) return FALSE;
  dialogClassRegistered = TRUE;
  return TRUE;
}

static BOOL dialog_gated_message(UINT message) {
  switch (message) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_NCMOUSEMOVE: case WM_NCLBUTTONDOWN: case WM_NCLBUTTONUP:
    case WM_COMMAND: case WM_SYSCOMMAND:
      return TRUE;
    default: return FALSE;
  }
}

enum {
  UI_PUMP_BEFORE_DISPATCH = 0,
  UI_PUMP_AFTER_DISPATCH = 1,
  UI_PUMP_QUIT = 2
};

typedef BOOL (*UiPumpEvent)(void *state, MSG *message, UINT phase);

static void ui_run_message_pump(HWND watched, HWND helper, BOOL *done,
                                DWORD *operationError,
                                DWORD *operationStatus, UiPumpEvent event,
                                void *state) {
  while (!*done) {
    MSG message = {0};
    const int received = GetMessage(&message, NULL, 0, 0);
    if (received < 0) {
      *operationError = GetLastError();
      *operationStatus = UndosGetLastStatus();
      break;
    }
    if (received == 0) {
      PostQuitMessage((int)message.wParam);
      *done = TRUE;
      if (event) (void)event(state, &message, UI_PUMP_QUIT);
      break;
    }
    BOOL dispatch = TRUE;
    if (event)
      dispatch = event(state, &message, UI_PUMP_BEFORE_DISPATCH);
    if (dispatch) (void)DispatchMessage(&message);
    if (event) (void)event(state, &message, UI_PUMP_AFTER_DISPATCH);
    if (watched && !ui_core_window_alive(watched)) *done = TRUE;
    if (helper && !ui_core_window_alive(helper)) *done = TRUE;
  }
}

static void dialog_capture_failure(UNDOS_UI_CONTEXT *context,
                                   DWORD *error, DWORD *status) {
  *error = GetLastError();
  *status = UndosGetLastStatus();
  if (!*status) {
    *error = context->operationError;
    *status = context->operationStatus;
  }
}

typedef struct UiDialogPump {
  UiDialogState *state;
  HWND owner;
  DWORD *operationError;
  DWORD *operationStatus;
  DWORD *cleanupError;
  DWORD *cleanupStatus;
} UiDialogPump;

static BOOL dialog_pump_event(void *opaque, MSG *message, UINT phase) {
  UiDialogPump *pump = (UiDialogPump *)opaque;
  UiDialogState *state = pump->state;
  if (phase == UI_PUMP_QUIT) {
    state->accepted = FALSE;
    return FALSE;
  }
  if (phase == UI_PUMP_BEFORE_DISPATCH) {
    if (message->hwnd == pump->owner && message->message == WM_CLOSE) {
      state->closeOwner = TRUE;
      state->done = TRUE;
      state->accepted = FALSE;
      return FALSE;
    }
    if (message->hwnd != state->window &&
        ui_core_window_in_family(message->hwnd, pump->owner) &&
        dialog_gated_message(message->message)) {
      if (message->message == WM_KEYDOWN || message->message == WM_KEYUP ||
          message->message == WM_CHAR)
        message->hwnd = state->window;
      else
        return FALSE;
    }
    if (message->hwnd != state->window &&
        ui_core_window_in_family(message->hwnd, pump->owner) &&
        message->message == WM_ACTIVATE && message->wParam) {
      if (!UndosSetFocus(state->window))
        dialog_capture_failure(state->context, pump->operationError,
                               pump->operationStatus);
      state->done = TRUE;
      return FALSE;
    }
    return TRUE;
  }
  if (!ui_core_window_alive(pump->owner)) {
    state->done = TRUE;
    state->accepted = FALSE;
  }
  if (state->failed) {
    *pump->operationError = state->operationError;
    *pump->operationStatus = state->operationStatus;
    *pump->cleanupError = state->cleanupError;
    *pump->cleanupStatus = state->cleanupStatus;
    state->done = TRUE;
  }
  return TRUE;
}

int UndosRunDialog(UNDOS_UI_CONTEXT *context, HWND owner, LPCSTR title,
                   LPCSTR message, UNDOS_DIALOG_FIELD *fields, UINT count,
                   BOOL confirm) {
  UINT titleLength = 0, messageLength = 0;
  if (!ui_context_valid(context) || !ui_core_window_alive(owner) ||
      count > 4 || (count && !fields) || (confirm != FALSE && confirm != TRUE) ||
      context->activeDepth >= 2 || dialogState.active ||
      !title || !message) {
    const BOOL busy = (context && context->activeDepth >= 2) ||
        (ui_context_valid(context) && dialogState.active);
    ui_fail(busy ? UI_STATUS_RETRY : UI_STATUS_INVALID_ARGUMENT,
            busy ? ERROR_RETRY : ERROR_INVALID_PARAMETER);
    if (ui_context_valid(context))
      ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
    return -1;
  }
  if ((uintptr_t)title <= UINT16_MAX || (uintptr_t)message <= UINT16_MAX) {
    ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
    return -1;
  }
  while (titleLength <= 63 && title[titleLength]) ++titleLength;
  while (messageLength <= 4096 && message[messageLength]) ++messageLength;
  if (titleLength > 63 || messageLength > 4096 ||
      !undos_utf8_valid(title, titleLength) ||
      !undos_utf8_valid(message, messageLength)) {
    ui_fail(titleLength > 63 || messageLength > 4096
        ? UI_STATUS_INVALID_SIZE : UI_STATUS_INVALID_ARGUMENT,
        titleLength > 63 || messageLength > 4096
        ? ERROR_INSUFFICIENT_BUFFER : ERROR_NO_UNICODE_TRANSLATION);
    ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
    return -1;
  }
  size_t total = (size_t)titleLength + 1 + messageLength + 1;
  UINT valueLengths[4] = {0};
  UINT labelLengths[4] = {0};
  for (UINT i = 0; i < count; ++i) {
    if (!fields[i].label || !fields[i].value || !fields[i].capacity ||
        fields[i].capacity > 1025 ||
        (uintptr_t)fields[i].label <= UINT16_MAX ||
        (uintptr_t)fields[i].value <= UINT16_MAX) {
      ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
      return -1;
    }
    while (labelLengths[i] <= 1024 && fields[i].label[labelLengths[i]])
      ++labelLengths[i];
    while (valueLengths[i] < fields[i].capacity &&
           fields[i].value[valueLengths[i]])
      ++valueLengths[i];
    if (labelLengths[i] > 1024 ||
        valueLengths[i] >= fields[i].capacity ||
        !undos_utf8_valid(fields[i].label, labelLengths[i]) ||
        !undos_utf8_valid(fields[i].value, valueLengths[i])) {
      ui_fail(valueLengths[i] >= fields[i].capacity ||
              labelLengths[i] > 1024 ? UI_STATUS_INVALID_SIZE
                                     : UI_STATUS_INVALID_ARGUMENT,
          valueLengths[i] >= fields[i].capacity ||
              labelLengths[i] > 1024 ? ERROR_INSUFFICIENT_BUFFER
                                     : ERROR_NO_UNICODE_TRANSLATION);
      ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
      return -1;
    }
    if (total > SIZE_MAX - (size_t)labelLengths[i] - 1 -
                            fields[i].capacity) {
      ui_fail(UI_STATUS_INVALID_SIZE, ERROR_INSUFFICIENT_BUFFER);
      ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
      return -1;
    }
    total += (size_t)labelLengths[i] + 1 + fields[i].capacity;
  }
  if (total > 16384) {
    ui_fail(UI_STATUS_INVALID_SIZE, ERROR_INSUFFICIENT_BUFFER);
    ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
    return -1;
  }
  char *storage = (char *)crt_malloc(total);
  if (!storage) {
    ui_fail(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
    return -1;
  }
  memset(&dialogState, 0, sizeof(dialogState));
  UiDialogState *state = &dialogState;
  state->active = TRUE;
  state->owner = owner;
  state->context = context;
  state->count = count;
  state->confirm = confirm;
  state->storage = storage;
  char *cursor = storage;
#define COPY_DIALOG_STRING(dst, src, n) do { (dst) = cursor; \
  memcpy(cursor, (src), (n) + 1); cursor += (n) + 1; } while (0)
  COPY_DIALOG_STRING(state->title, title, titleLength);
  COPY_DIALOG_STRING(state->message, message, messageLength);
  for (UINT i = 0; i < count; ++i) {
    COPY_DIALOG_STRING(state->labels[i], fields[i].label, labelLengths[i]);
    state->values[i] = cursor;
    memcpy(cursor, fields[i].value, valueLengths[i] + 1);
    cursor += fields[i].capacity;
    state->destinations[i] = fields[i].value;
    state->capacities[i] = fields[i].capacity;
  }
#undef COPY_DIALOG_STRING
  DWORD operationError = 0, operationStatus = 0;
  DWORD cleanupError = 0, cleanupStatus = 0;
  int created = 0;
  UINT screenWidth = 0, screenHeight = 0;
  if (!UndosGetDisplaySize(context, &screenWidth, &screenHeight) ||
      !ensure_dialog_class()) {
    dialog_capture_failure(context, &operationError, &operationStatus);
    goto dialog_done;
  }
  state->screenWidth = screenWidth;
  state->screenHeight = screenHeight;
  if (!dialog_layout(state)) {
    dialog_capture_failure(context, &operationError, &operationStatus);
    goto dialog_done;
  }
  state->x = (int)(screenWidth - (UINT)state->width) / 2;
  state->y = (int)(screenHeight - (UINT)state->height) / 2;
  state->window = CreateWindow("UndosUiDialog", state->title,
      WS_VISIBLE | WS_CAPTION | WS_BORDER,
      state->x, state->y,
      state->width, state->height, NULL, NULL, NULL, NULL);
  if (!state->window) {
    operationError = GetLastError();
    operationStatus = UndosGetLastStatus();
    goto dialog_active_done;
  }
  created = 1;
  if (!UndosAttachUiContext(state->window, context)) {
    operationError = GetLastError();
    operationStatus = UndosGetLastStatus();
    goto dialog_active_done;
  }
  ++context->activeDepth;
  state->ownsDepth = TRUE;
  if (!UndosSetFocus(state->window) ||
      !InvalidateRect(state->window, NULL, TRUE)) {
    operationError = GetLastError();
    operationStatus = UndosGetLastStatus();
    goto dialog_active_done;
  }
  dialog_reveal(state);
  UiDialogPump pump = {state, owner, &operationError, &operationStatus,
                       &cleanupError, &cleanupStatus};
  ui_run_message_pump(owner, state->window, &state->done, &operationError,
                      &operationStatus, dialog_pump_event, &pump);
  if (!cleanupStatus && !state->cleanupStatus &&
      !dialog_finish_paint(state)) {
    cleanupError = state->cleanupError;
    cleanupStatus = state->cleanupStatus;
  }
  if (!operationStatus && state->operationStatus) {
    operationError = state->operationError;
    operationStatus = state->operationStatus;
  }
dialog_active_done:
  if (state->ownsDepth && context->activeDepth) --context->activeDepth;
  if (state->cleanupStatus) {
    if (state->hasPreviousFont &&
        ui_core_retain_font(state->window, state->previousFont))
      state->hasPreviousFont = FALSE;
    if (!operationStatus && state->operationStatus) {
      operationError = state->operationError;
      operationStatus = state->operationStatus;
    }
    cleanupError = state->cleanupError;
    cleanupStatus = state->cleanupStatus;
    if (created && !state->hasPreviousFont) {
      (void)ui_core_mark_ui_recovery(state->window, operationError,
          operationStatus, cleanupError, cleanupStatus);
      created = 0;
    }
  }
  if (created) {
    if (ui_core_window_alive(state->window) &&
        !UndosAttachUiContext(state->window, NULL)) {
      operationError = GetLastError();
      operationStatus = UndosGetLastStatus();
    }
    if (!DestroyWindow(state->window)) {
      cleanupError = GetLastError();
      cleanupStatus = UndosGetLastStatus();
      (void)ui_core_mark_ui_recovery(state->window, operationError,
          operationStatus, cleanupError, cleanupStatus);
    }
  }
  if (!operationStatus && !state->closeOwner &&
      ui_core_window_alive(owner) && !UndosSetFocus(owner)) {
    operationError = GetLastError();
    operationStatus = UndosGetLastStatus();
  }
  if (state->closeOwner && ui_core_window_alive(owner)) {
    if (!PostMessage(owner, WM_CLOSE, 0, 0) && !operationStatus) {
      operationError = GetLastError();
      operationStatus = UndosGetLastStatus();
    }
  }
dialog_done:
  if (operationStatus || cleanupStatus) {
    context->_sdkState = UI_CONTEXT_MAGIC;
    ui_result(context, -1, operationError, operationStatus,
              cleanupError, cleanupStatus);
    ui_fail(operationStatus ? operationStatus : cleanupStatus,
            operationStatus ? operationError : cleanupError);
    state->active = FALSE;
    state->storage = NULL;
    crt_free(storage);
    memset(state, 0, sizeof(*state));
    return -1;
  }
  if (state->accepted) {
    for (UINT i = 0; i < count; ++i)
      memcpy(state->destinations[i], state->values[i],
             strlen(state->values[i]) + 1);
  }
  const int result = state->accepted ? 1 : 0;
  ui_result(context, result, 0, 0, 0, 0);
  state->active = FALSE;
  state->storage = NULL;
  crt_free(storage);
  memset(state, 0, sizeof(*state));
  return result;
}

typedef struct UiMenuState {
  BOOL active;
  BOOL done;
  BOOL failed;
  BOOL ownsDepth;
  HWND owner;
  HWND window;
  UNDOS_UI_CONTEXT *context;
  UINT screenWidth;
  UINT screenHeight;
  UINT width;
  UINT height;
  UINT rowHeight;
  UINT firstRow;
  UINT visibleRows;
  UINT selected;
  int x;
  int y;
  int result;
  PAINTSTRUCT paint;
  BOOL paintActive;
  UNDOS_FONT previousFont;
  BOOL hasPreviousFont;
  DWORD operationError;
  DWORD operationStatus;
  DWORD cleanupError;
  DWORD cleanupStatus;
} UiMenuState;

static UiMenuState menuState;
static BOOL menuClassRegistered;

typedef struct UiMenuPump {
  UiMenuState *state;
  DWORD *operationError;
  DWORD *operationStatus;
  DWORD *cleanupError;
  DWORD *cleanupStatus;
} UiMenuPump;

static BOOL menu_pump_event(void *opaque, MSG *message, UINT phase) {
  UiMenuPump *pump = (UiMenuPump *)opaque;
  UiMenuState *state = pump->state;
  if (phase == UI_PUMP_QUIT) {
    state->done = TRUE;
    return FALSE;
  }
  if (phase == UI_PUMP_BEFORE_DISPATCH) {
    if (message->hwnd != state->window &&
        ui_core_window_in_family(message->hwnd, state->owner) &&
        (message->message == WM_KEYDOWN || message->message == WM_KEYUP ||
         message->message == WM_CHAR))
      message->hwnd = state->window;
    return TRUE;
  }
  if (!ui_core_window_alive(state->owner)) state->done = TRUE;
  if (state->failed) {
    *pump->operationError = state->operationError;
    *pump->operationStatus = state->operationStatus;
    *pump->cleanupError = state->cleanupError;
    *pump->cleanupStatus = state->cleanupStatus;
    state->done = TRUE;
  }
  return TRUE;
}

#ifdef UNDOSGUI_TESTING
void undosgui_ui_test_reset(void) {
  memset(&dialogState, 0, sizeof(dialogState));
  memset(&menuState, 0, sizeof(menuState));
  dialogClassRegistered = FALSE;
  menuClassRegistered = FALSE;
}
#endif

static const char *const systemMenuLabels[] = {
  "Restore", "Move (arrows)", "Size (arrows)",
  "Minimize", "Maximize", "Close"
};
static const UINT systemMenuCommands[] = {
  SC_RESTORE, SC_MOVE, SC_SIZE, SC_MINIMIZE, SC_MAXIMIZE, SC_CLOSE
};

static void menu_error(UiMenuState *state, DWORD error, DWORD status) {
  if (state->operationStatus) return;
  state->operationError = error;
  state->operationStatus = status;
  state->failed = TRUE;
  state->done = TRUE;
}

static void menu_cleanup_error(UiMenuState *state) {
  state->failed = TRUE;
  state->done = TRUE;
}

static BOOL menu_finish_paint(UiMenuState *state) {
  if (state->paintActive) {
    if (!EndPaint(state->window, &state->paint)) {
      state->cleanupError = GetLastError();
      state->cleanupStatus = UndosGetLastStatus();
      return FALSE;
    }
    state->paintActive = FALSE;
  }
  if (state->hasPreviousFont) {
    if (!UndosCloseFont(state->previousFont)) {
      state->cleanupError = GetLastError();
      state->cleanupStatus = UndosGetLastStatus();
      if (ui_core_retain_font(state->window, state->previousFont))
        state->hasPreviousFont = FALSE;
      return FALSE;
    }
    state->hasPreviousFont = FALSE;
  }
  return TRUE;
}

static BOOL menu_layout(UiMenuState *state) {
  if (!state->context->metricsValid ||
      state->context->metrics.lineHeight > INT_MAX - 4)
    return FALSE;
  state->rowHeight = state->context->metrics.lineHeight + 4;
  UINT textWidth = 0;
  for (UINT i = 0; i < 6; ++i) {
    UNDOS_TEXT_EXTENT extent = {0};
    if (!UndosMeasureText(state->context->font, UNDOS_TEXT_REGULAR,
        UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, systemMenuLabels[i],
        &extent))
      return FALSE;
    if (extent.advanceWidth > textWidth) textWidth = extent.advanceWidth;
  }
  state->width = textWidth + 12;
  if (state->width > state->screenWidth) state->width = state->screenWidth;
  if (!state->width || state->screenHeight < 4) return FALSE;
  state->visibleRows = (state->screenHeight - 2) / state->rowHeight;
  if (!state->visibleRows) state->visibleRows = 1;
  if (state->visibleRows > 6) state->visibleRows = 6;
  uint64_t height = (uint64_t)state->visibleRows * state->rowHeight + 2;
  if (height > state->screenHeight) height = state->screenHeight;
  state->height = (UINT)height;
  return TRUE;
}

static void menu_paint(UiMenuState *state) {
  if (!menu_finish_paint(state)) {
    menu_cleanup_error(state);
    return;
  }
  memset(&state->paint, 0, sizeof(state->paint));
  state->paint.hdc = BeginPaint(state->window, &state->paint);
  if (!state->paint.hdc) {
    menu_error(state, GetLastError(), UndosGetLastStatus());
    return;
  }
  state->paintActive = TRUE;
  BOOL had = FALSE;
  if (!UndosSelectFont(state->paint.hdc, state->context->font,
                       &state->previousFont, &had)) {
    menu_error(state, GetLastError(), UndosGetLastStatus());
    (void)menu_finish_paint(state);
    return;
  }
  state->hasPreviousFont = had;
  if (had && !UndosCloseFont(state->previousFont)) {
    state->cleanupError = GetLastError();
    state->cleanupStatus = UndosGetLastStatus();
    if (ui_core_retain_font(state->window, state->previousFont))
      state->hasPreviousFont = FALSE;
    menu_cleanup_error(state);
    if (EndPaint(state->window, &state->paint)) state->paintActive = FALSE;
    return;
  }
  state->hasPreviousFont = FALSE;
  HDC dc = state->paint.hdc;
  for (UINT i = state->firstRow;
       i < 6 && i < state->firstRow + state->visibleRows; ++i) {
    const int top = (int)(i - state->firstRow) * (int)state->rowHeight;
    const UINT background = i == state->selected ? 1u : 7u;
    if (!fill_color(dc, UndosPaletteColor(background), 0, top,
                    (int)state->width, top + (int)state->rowHeight) ||
        !dialog_draw_text(dc, 5, top + 2, systemMenuLabels[i],
                          i == state->selected ? 15u : 0u, background)) {
      menu_error(state, GetLastError(), UndosGetLastStatus());
      break;
    }
  }
  if (!menu_finish_paint(state) && !state->operationStatus)
    menu_cleanup_error(state);
}

static LRESULT CALLBACK menu_proc(HWND window, UINT message, WPARAM wParam,
                                  LPARAM lParam) {
  UiMenuState *state = &menuState;
  if (!state->active || window != state->window)
    return DefWindowProc(window, message, wParam, lParam);
  if (message == WM_PAINT) {
    menu_paint(state);
  } else if (message == WM_CLOSE || message == WM_DESTROY ||
             (message == WM_ACTIVATE && !wParam)) {
    state->done = TRUE;
  } else if (message == WM_FONTCHANGED) {
    if (!UndosRefreshUi(state->context) ||
        !UndosGetDisplaySize(state->context, &state->screenWidth,
                             &state->screenHeight) ||
        !menu_layout(state)) {
      menu_error(state, GetLastError(), UndosGetLastStatus());
    } else {
      if (state->selected < state->firstRow)
        state->firstRow = state->selected;
      else if (state->selected >= state->firstRow + state->visibleRows)
        state->firstRow = state->selected - state->visibleRows + 1;
      (void)InvalidateRect(window, NULL, TRUE);
    }
  } else if (message == WM_DISPLAYCHANGE) {
    if (!UndosGetDisplaySize(state->context, &state->screenWidth,
                             &state->screenHeight) ||
        !menu_layout(state)) {
      menu_error(state, GetLastError(), UndosGetLastStatus());
    } else {
      if (state->x + (int)state->width > (int)state->screenWidth)
        state->x = (int)state->screenWidth - (int)state->width;
      if (state->y + (int)state->height > (int)state->screenHeight)
        state->y = (int)state->screenHeight - (int)state->height;
      if (state->x < 0) state->x = 0;
      if (state->y < 0) state->y = 0;
      if (!MoveWindow(window, state->x, state->y, (int)state->width,
                      (int)state->height, TRUE)) {
        menu_error(state, GetLastError(), UndosGetLastStatus());
      } else {
        if (state->selected < state->firstRow)
          state->firstRow = state->selected;
        else if (state->selected >= state->firstRow + state->visibleRows)
          state->firstRow = state->selected - state->visibleRows + 1;
        (void)InvalidateRect(window, NULL, TRUE);
      }
    }
  } else if (message == WM_KEYDOWN) {
    if (wParam == VK_ESCAPE) {
      state->done = TRUE;
    } else if (wParam == VK_UP || wParam == VK_DOWN) {
      state->selected = (state->selected + (wParam == VK_UP ? 5 : 1)) % 6;
      if (state->selected < state->firstRow)
        state->firstRow = state->selected;
      else if (state->selected >= state->firstRow + state->visibleRows)
        state->firstRow = state->selected - state->visibleRows + 1;
      (void)InvalidateRect(window, NULL, TRUE);
    } else if (wParam == VK_RETURN || wParam == VK_SPACE) {
      state->result = (int)state->selected;
      state->done = TRUE;
    }
  } else if (message == WM_LBUTTONDOWN) {
    const int y = (int16_t)(lParam >> 16);
    const UINT row = state->firstRow +
        (y < 0 ? 6u : (UINT)y / state->rowHeight);
    if (row < 6) {
      state->result = (int)row;
      state->done = TRUE;
    }
  } else {
    return DefWindowProc(window, message, wParam, lParam);
  }
  return 0;
}

static BOOL ensure_menu_class(void) {
  if (menuClassRegistered) return TRUE;
  WNDCLASS klass = {0};
  klass.lpfnWndProc = menu_proc;
  klass.hbrBackground = UNDOS_PALETTE_BRUSH(7);
  klass.lpszClassName = "UndosUiMenu";
  if (!RegisterClass(&klass)) return FALSE;
  menuClassRegistered = TRUE;
  return TRUE;
}

static BOOL menu_recovery(HWND window, DWORD operationError,
                          DWORD operationStatus, DWORD *cleanupError,
                          DWORD *cleanupStatus) {
  if (DestroyWindow(window)) return TRUE;
  *cleanupError = GetLastError();
  *cleanupStatus = UndosGetLastStatus();
  (void)ui_core_mark_ui_recovery(window, operationError, operationStatus,
                                 *cleanupError, *cleanupStatus);
  return FALSE;
}

static BOOL menu_retain_pending_font(UiMenuState *state) {
  if (!state->hasPreviousFont) return TRUE;
  if (!ui_core_retain_font(state->window, state->previousFont)) return FALSE;
  state->hasPreviousFont = FALSE;
  return TRUE;
}

static void run_system_menu(HWND owner, LPARAM hitPoint) {
  UNDOS_UI_CONTEXT *context = ui_core_window_context(owner);
  if (!context || !ui_context_valid(context) || menuState.active ||
      context->activeDepth >= 2) {
    if (context && ui_context_valid(context)) {
      ui_fail(UI_STATUS_RETRY, ERROR_RETRY);
      ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
    }
    return;
  }
  memset(&menuState, 0, sizeof(menuState));
  UiMenuState *state = &menuState;
  state->active = TRUE;
  state->owner = owner;
  state->context = context;
  state->result = -1;
  DWORD operationError = 0, operationStatus = 0;
  DWORD cleanupError = 0, cleanupStatus = 0;
  if (!UndosGetDisplaySize(context, &state->screenWidth,
                           &state->screenHeight) ||
      !ensure_menu_class() || !menu_layout(state)) {
    operationError = GetLastError();
    operationStatus = UndosGetLastStatus();
    goto menu_done;
  }
  int x = (int16_t)(hitPoint & 0xFFFF);
  int y = (int16_t)(hitPoint >> 16);
  if (x + (int)state->width > (int)state->screenWidth)
    x = (int)state->screenWidth - (int)state->width;
  if (y + (int)state->height > (int)state->screenHeight)
    y = (int)state->screenHeight - (int)state->height;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  state->x = x;
  state->y = y;
  state->window = CreateWindow("UndosUiMenu", "",
      WS_VISIBLE | WS_BORDER, x, y, (int)state->width,
      (int)state->height, NULL, NULL, NULL, NULL);
  if (!state->window) {
    operationError = GetLastError();
    operationStatus = UndosGetLastStatus();
    goto menu_done;
  }
  if (!UndosAttachUiContext(state->window, context)) {
    operationError = GetLastError();
    operationStatus = UndosGetLastStatus();
    goto menu_cleanup;
  }
  ++context->activeDepth;
  state->ownsDepth = TRUE;
  if (!UndosSetFocus(state->window)) {
    operationError = GetLastError();
    operationStatus = UndosGetLastStatus();
    goto menu_cleanup;
  }
  UiMenuPump pump = {state, &operationError, &operationStatus,
                     &cleanupError, &cleanupStatus};
  ui_run_message_pump(owner, state->window, &state->done, &operationError,
                      &operationStatus, menu_pump_event, &pump);
menu_cleanup:
  if (!cleanupStatus && !state->cleanupStatus &&
      !menu_finish_paint(state)) {
    cleanupError = state->cleanupError;
    cleanupStatus = state->cleanupStatus;
  }
  if (!operationStatus && state->operationStatus) {
    operationError = state->operationError;
    operationStatus = state->operationStatus;
  }
  if (!cleanupStatus && state->cleanupStatus) {
    cleanupError = state->cleanupError;
    cleanupStatus = state->cleanupStatus;
  }
  if (!operationStatus && ui_core_window_alive(owner) &&
      !UndosSetFocus(owner)) {
    operationError = GetLastError();
    operationStatus = UndosGetLastStatus();
  }
  if (state->ownsDepth && context->activeDepth) {
    --context->activeDepth;
    state->ownsDepth = FALSE;
  }
  BOOL recoveryOnly = FALSE;
  if (cleanupStatus) {
    recoveryOnly = TRUE;
    if (menu_retain_pending_font(state) &&
        ui_core_mark_ui_recovery(state->window, operationError,
                                 operationStatus, cleanupError,
                                 cleanupStatus)) {
    } else if (!operationStatus) {
      operationError = GetLastError();
      operationStatus = UndosGetLastStatus();
    }
    state->result = -1;
  }
  if (!recoveryOnly && ui_core_window_alive(state->window))
    (void)UndosAttachUiContext(state->window, NULL);
  if (state->window && !recoveryOnly) {
    (void)menu_recovery(state->window, operationError, operationStatus,
                        &cleanupError, &cleanupStatus);
  }
  if (!operationStatus && state->result >= 0 &&
      ui_core_window_alive(owner) &&
      !PostMessage(owner, WM_SYSCOMMAND, systemMenuCommands[state->result], 0)) {
    operationError = GetLastError();
    operationStatus = UndosGetLastStatus();
  }
menu_done:
  if (operationStatus || cleanupStatus) {
    ui_result(context, -1, operationError, operationStatus,
              cleanupError, cleanupStatus);
    ui_fail(operationStatus ? operationStatus : cleanupStatus,
            operationStatus ? operationError : cleanupError);
  } else {
    ui_result(context, state->result >= 0 ? 1 : 0, 0, 0,
              cleanupError, cleanupStatus);
  }
  state->active = FALSE;
  memset(state, 0, sizeof(*state));
}

typedef struct UiGeometryPump {
  HWND window;
  UNDOS_UI_CONTEXT *context;
  BOOL size;
  BOOL done;
  BOOL failed;
  RECT original;
  RECT current;
  int originX;
  int originY;
  UINT screenWidth;
  UINT screenHeight;
  DWORD error;
  DWORD status;
} UiGeometryPump;

static BOOL geometry_pump_event(void *opaque, MSG *message, UINT phase) {
  UiGeometryPump *pump = (UiGeometryPump *)opaque;
  if (phase == UI_PUMP_QUIT) {
    pump->done = TRUE;
    return FALSE;
  }
  if (phase == UI_PUMP_BEFORE_DISPATCH) {
    if (message->hwnd != pump->window &&
        ui_core_window_in_family(message->hwnd, pump->window) &&
        message->message == WM_KEYDOWN) {
      message->hwnd = pump->window;
    } else if (message->hwnd != pump->window &&
               ui_core_window_in_family(message->hwnd, pump->window) &&
               dialog_gated_message(message->message)) {
      return FALSE;
    }
    return TRUE;
  }
  if (!ui_core_window_alive(pump->window)) {
    pump->done = TRUE;
    return TRUE;
  }
  if (message->message == WM_DISPLAYCHANGE) {
    if (!UndosGetDisplaySize(pump->context, &pump->screenWidth,
                             &pump->screenHeight)) {
      pump->error = GetLastError();
      pump->status = UndosGetLastStatus();
      pump->failed = TRUE;
      pump->done = TRUE;
      return TRUE;
    }
    const int64_t width = (int64_t)pump->current.right -
        pump->current.left;
    const int64_t height = (int64_t)pump->current.bottom -
        pump->current.top;
    if (width <= 0 || height <= 0 || width > pump->screenWidth ||
        height > pump->screenHeight) {
      ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      pump->error = GetLastError();
      pump->status = UndosGetLastStatus();
      pump->failed = TRUE;
      pump->done = TRUE;
      return TRUE;
    }
    int64_t left = pump->current.left, top = pump->current.top;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (left + width > pump->screenWidth)
      left = pump->screenWidth - width;
    if (top + height > pump->screenHeight)
      top = pump->screenHeight - height;
    const int64_t childX = left - pump->originX;
    const int64_t childY = top - pump->originY;
    if (childX < INT16_MIN || childX > INT16_MAX ||
        childY < INT16_MIN || childY > INT16_MAX ||
        !MoveWindow(pump->window, (int)childX, (int)childY,
                    (int)width, (int)height, TRUE)) {
      if (GetLastError() == ERROR_SUCCESS)
        ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      pump->error = GetLastError();
      pump->status = UndosGetLastStatus();
      pump->failed = TRUE;
      pump->done = TRUE;
      return TRUE;
    }
    pump->current.left = (int)left;
    pump->current.top = (int)top;
    pump->current.right = (int)(left + width);
    pump->current.bottom = (int)(top + height);
  }
  if (message->hwnd != pump->window || message->message != WM_KEYDOWN)
    return TRUE;
  if (message->wParam == VK_RETURN) {
    pump->done = TRUE;
  } else if (message->wParam == VK_ESCAPE) {
    if (!MoveWindow(pump->window,
          pump->original.left - pump->originX,
          pump->original.top - pump->originY,
          pump->original.right - pump->original.left,
          pump->original.bottom - pump->original.top, TRUE)) {
      pump->error = GetLastError();
      pump->status = UndosGetLastStatus();
      pump->failed = TRUE;
    }
    pump->done = TRUE;
  } else {
    int dx = 0, dy = 0;
    if (message->wParam == VK_LEFT) dx = -8;
    if (message->wParam == VK_RIGHT) dx = 8;
    if (message->wParam == VK_UP) dy = -8;
    if (message->wParam == VK_DOWN) dy = 8;
    if (pump->size) {
      if ((int64_t)pump->current.right + dx - pump->current.left >= 150)
        pump->current.right += dx;
      if ((int64_t)pump->current.bottom + dy - pump->current.top >= 100)
        pump->current.bottom += dy;
    } else {
      pump->current.left += dx;
      pump->current.right += dx;
      pump->current.top += dy;
      pump->current.bottom += dy;
    }
    if ((dx || dy) &&
        !MoveWindow(pump->window,
            pump->current.left - pump->originX,
            pump->current.top - pump->originY,
            pump->current.right - pump->current.left,
            pump->current.bottom - pump->current.top, TRUE)) {
      pump->error = GetLastError();
      pump->status = UndosGetLastStatus();
      pump->failed = TRUE;
      pump->done = TRUE;
    }
  }
  return TRUE;
}

static BOOL geometry_operation(HWND window, BOOL size) {
  UNDOS_UI_CONTEXT *context = ui_core_window_context(window);
  if (!context || !ui_context_valid(context) || context->activeDepth >= 2) {
    ui_fail(context ? UI_STATUS_RETRY : UI_STATUS_UNSUPPORTED,
            context ? ERROR_RETRY : ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
  }
  ++context->activeDepth;
  RECT original = {0};
  if (!UndosShowWindow(window, SW_RESTORE) ||
      !GetWindowRect(window, &original)) {
    ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
    --context->activeDepth;
    return FALSE;
  }
  const uint32_t parentValue = ui_core_window_parent(window);
  int originX = 0, originY = 0;
  if (parentValue) {
    RECT outer = {0}, client = {0};
    HWND parent = (HWND)(uintptr_t)parentValue;
    if (!GetWindowRect(parent, &outer) || !GetClientRect(parent, &client)) {
      ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
      --context->activeDepth;
      return FALSE;
    }
    const int64_t outerWidth = (int64_t)outer.right - outer.left;
    const int64_t clientWidth = (int64_t)client.right - client.left;
    const int64_t outerHeight = (int64_t)outer.bottom - outer.top;
    const int64_t clientHeight = (int64_t)client.bottom - client.top;
    if (outerWidth < clientWidth || outerHeight < clientHeight) {
      ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
      --context->activeDepth;
      return FALSE;
    }
    const int64_t borderX = (outerWidth - clientWidth) / 2;
    const int64_t borderY = (outerHeight - clientHeight - 20) / 2;
    const int64_t ox = (int64_t)outer.left + borderX;
    const int64_t oy = (int64_t)outer.top + borderY;
    if (ox < INT16_MIN || ox > INT16_MAX ||
        oy < INT16_MIN || oy > INT16_MAX) {
      ui_fail(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      ui_result(context, -1, GetLastError(), UndosGetLastStatus(), 0, 0);
      --context->activeDepth;
      return FALSE;
    }
    originX = (int)ox;
    originY = (int)oy;
  }
  UiGeometryPump pump = {0};
  pump.window = window;
  pump.context = context;
  pump.size = size;
  pump.original = original;
  pump.current = original;
  pump.originX = originX;
  pump.originY = originY;
  if (!UndosGetDisplaySize(context, &pump.screenWidth,
                           &pump.screenHeight)) {
    pump.error = GetLastError();
    pump.status = UndosGetLastStatus();
    --context->activeDepth;
    ui_result(context, -1, pump.error, pump.status, 0, 0);
    return FALSE;
  }
  ui_run_message_pump(window, window, &pump.done, &pump.error, &pump.status,
                      geometry_pump_event, &pump);
  --context->activeDepth;
  if (pump.failed || pump.status) {
    ui_result(context, -1, pump.error, pump.status, 0, 0);
    return FALSE;
  }
  ui_result(context, 1, 0, 0, 0, 0);
  return TRUE;
}

void ui_default_system_menu(HWND window, LPARAM hitPoint) {
  run_system_menu(window, hitPoint);
}

void ui_default_system_geometry(HWND window, UINT command) {
  (void)geometry_operation(window, command == SC_SIZE);
}
