#ifndef UNDOSGUI_EXTENSIONS_H
#define UNDOSGUI_EXTENSIONS_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNDOS_PALETTE_BRUSH(index) ((HBRUSH)(uintptr_t)((index) + 1u))

typedef uint32_t UNDOS_FONT;

typedef struct UNDOS_WINDOW_CREATION_RECOVERY {
  HWND window;
  DWORD setupError;
  DWORD setupStatus;
  DWORD cleanupError;
  DWORD cleanupStatus;
} UNDOS_WINDOW_CREATION_RECOVERY;

typedef struct UNDOS_UI_CLEANUP_RECOVERY {
  HWND window;
  DWORD operationError;
  DWORD operationStatus;
  DWORD cleanupError;
  DWORD cleanupStatus;
} UNDOS_UI_CLEANUP_RECOVERY;

typedef struct UNDOS_DIALOG_FIELD {
  LPCSTR label;
  LPSTR value;
  UINT capacity;
} UNDOS_DIALOG_FIELD;

enum UndosFontSource {
  UNDOS_FONT_SYSTEM_DEFAULT = 0,
  UNDOS_FONT_BUILTIN_BITMAP = 1
};

enum UndosGlyphFallback {
  UNDOS_GLYPH_FALLBACK_NOTDEF = 0,
  UNDOS_GLYPH_FALLBACK_BITMAP = 1
};

enum UndosTextStyle {
  UNDOS_TEXT_REGULAR = 0,
  UNDOS_TEXT_BOLD = 1
};

enum UndosTextLayout {
  UNDOS_TEXT_PROPORTIONAL = 0,
  UNDOS_TEXT_UNIFORM_GRID = 1
};

enum UndosTextRepertoire {
  UNDOS_TEXT_BASIC = 0,
  UNDOS_TEXT_TERMINAL = 1
};

typedef struct UNDOS_TEXT_METRICS {
  uint32_t generation;
  uint32_t ascent;
  uint32_t descent;
  uint32_t lineGap;
  uint32_t lineHeight;
  uint32_t baselineOffset;
  uint32_t cellWidth;
  uint32_t cellHeight;
  int32_t inkLeft;
  int32_t inkTop;
  int32_t inkRight;
  int32_t inkBottom;
} UNDOS_TEXT_METRICS;

typedef struct UNDOS_TEXT_EXTENT {
  uint32_t generation;
  uint32_t advanceWidth;
  uint32_t lineHeight;
  uint32_t baselineOffset;
  int32_t inkLeft;
  int32_t inkTop;
  int32_t inkRight;
  int32_t inkBottom;
} UNDOS_TEXT_EXTENT;

typedef struct UNDOS_UI_CONTEXT {
  UNDOS_FONT font;
  BOOL hasFont;
  UNDOS_TEXT_METRICS metrics;
  BOOL metricsValid;
  UNDOS_FONT deferredFont;
  BOOL hasDeferredFont;
  HDC displayDc;
  BOOL hasDisplayDc;
  BOOL displayDcStale;
  uint32_t activeDepth;
  uint32_t resultSerial;
  int32_t result;
  DWORD operationError;
  DWORD operationStatus;
  DWORD cleanupError;
  DWORD cleanupStatus;
  uint32_t _sdkBorrowers;
  uint32_t _sdkState;
} UNDOS_UI_CONTEXT;

DWORD UndosGetLastStatus(void);
BOOL UndosGetWindowCreationRecovery(
    UNDOS_WINDOW_CREATION_RECOVERY *recovery);
BOOL UndosGetUiCleanupRecovery(UNDOS_UI_CLEANUP_RECOVERY *recovery);
BOOL UndosShowWindow(HWND window, int command);
BOOL UndosSetFocus(HWND window);
BOOL UndosInitUi(UNDOS_UI_CONTEXT *context);
BOOL UndosRefreshUi(UNDOS_UI_CONTEXT *context);
BOOL UndosCloseUi(UNDOS_UI_CONTEXT *context);
BOOL UndosAttachUiContext(HWND window, UNDOS_UI_CONTEXT *context);
BOOL UndosGetDisplaySize(UNDOS_UI_CONTEXT *context, UINT *width, UINT *height);
COLORREF UndosPaletteColor(UINT index);
BOOL UndosAcquireFont(enum UndosFontSource source,
                      enum UndosGlyphFallback fallback,
                      UNDOS_FONT *font, uint32_t *generation);
BOOL UndosCloseFont(UNDOS_FONT font);
BOOL UndosSelectFont(HDC dc, UNDOS_FONT font, UNDOS_FONT *previous,
                     BOOL *hadPrevious);
BOOL UndosGetFontMetrics(UNDOS_FONT font, enum UndosTextStyle style,
                         enum UndosTextRepertoire repertoire,
                         UNDOS_TEXT_METRICS *metrics);
BOOL UndosMeasureText(UNDOS_FONT font, enum UndosTextStyle style,
                      enum UndosTextLayout layout,
                      enum UndosTextRepertoire repertoire, LPCSTR text,
                      UNDOS_TEXT_EXTENT *extent);
BOOL UndosDrawText(HDC dc, int x, int y, enum UndosTextStyle style,
                   enum UndosTextLayout layout,
                   enum UndosTextRepertoire repertoire, LPCSTR text);
BOOL UndosFillRectangle(HDC dc, int left, int top, int right, int bottom);
BOOL UndosMoveTo(HDC dc, int x, int y);
BOOL UndosLineTo(HDC dc, int x, int y);
BOOL UndosFitTextPrefix(UNDOS_FONT font, enum UndosTextStyle style,
                        enum UndosTextLayout layout,
                        enum UndosTextRepertoire repertoire, LPCSTR text,
                        LPSTR output, UINT capacity, UINT pixelWidth,
                        UNDOS_TEXT_EXTENT *extent);
BOOL UndosFitTextTail(UNDOS_FONT font, enum UndosTextStyle style,
                      enum UndosTextLayout layout,
                      enum UndosTextRepertoire repertoire, LPCSTR text,
                      LPSTR output, UINT capacity, UINT pixelWidth,
                      UNDOS_TEXT_EXTENT *extent);
BOOL UndosGetButtonHeight(UNDOS_FONT font, LPCSTR label, int *height);
BOOL UndosDrawButton(HDC dc, UNDOS_FONT font, int x, int y, int width,
                     LPCSTR label, BOOL focused);
BOOL UndosDrawClassicIcon(HDC dc, int x, int y, UINT kind, BOOL selected);
int UndosRunDialog(UNDOS_UI_CONTEXT *context, HWND owner, LPCSTR title,
                   LPCSTR message, UNDOS_DIALOG_FIELD *fields, UINT count,
                   BOOL confirm);

#ifdef __cplusplus
}
#endif
#endif
