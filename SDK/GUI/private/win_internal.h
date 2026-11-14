#ifndef UNDOSGUI_WIN_INTERNAL_H
#define UNDOSGUI_WIN_INTERNAL_H

#include <windows.h>
#include <undosgui/extensions.h>

#include "backend.h"

/* Internal names shared by the windows*.c files. */
#define classes undosgui_win_classes
#define windows undosgui_win_windows
#define dcs undosgui_win_dcs
#define lastError undosgui_win_lastError
#define lastStatus undosgui_win_lastStatus
#define quitPending undosgui_win_quitPending
#define quitCode undosgui_win_quitCode
#define nextAtom undosgui_win_nextAtom
#define nextWindowIdentity undosgui_win_nextWindowIdentity
#define keyState undosgui_win_keyState
#define backend_dc_colors undosgui_win_backend_dc_colors
#define backend_ok undosgui_win_backend_ok
#define call_window_procedure undosgui_win_call_window_procedure
#define class_by_name undosgui_win_class_by_name
#define class_slot undosgui_win_class_slot
#define cleanup_window_dcs undosgui_win_cleanup_window_dcs
#define copy_message undosgui_win_copy_message
#define dc_by_handle undosgui_win_dc_by_handle
#define dc_slot undosgui_win_dc_slot
#define error_for_status undosgui_win_error_for_status
#define failure undosgui_win_failure
#define forget_descendants undosgui_win_forget_descendants
#define forget_window undosgui_win_forget_window
#define forget_window_dcs undosgui_win_forget_window_dcs
#define hdc_from_value undosgui_win_hdc_from_value
#define hdc_value undosgui_win_hdc_value
#define hwnd_from_value undosgui_win_hwnd_from_value
#define hwnd_value undosgui_win_hwnd_value
#define live_window_by_handle undosgui_win_live_window_by_handle
#define local_failure undosgui_win_local_failure
#define nearest_palette undosgui_win_nearest_palette
#define note_key undosgui_win_note_key
#define quit_message undosgui_win_quit_message
#define set_dc_color undosgui_win_set_dc_color
#define style_from_native undosgui_win_style_from_native
#define style_to_native undosgui_win_style_to_native
#define translate_show undosgui_win_translate_show
#define utf8_string undosgui_win_utf8_string
#define valid_color undosgui_win_valid_color
#define valid_window_style undosgui_win_valid_window_style
#define window_by_handle undosgui_win_window_by_handle
#define window_slot undosgui_win_window_slot

#define UI_CLASS_LIMIT 32
#define UI_WINDOW_LIMIT 64
#define UI_DC_LIMIT 16
#define UI_CLASS_NAME_BYTES 31
#define UI_TITLE_BYTES 63
#define UI_WND_EXTRA_BYTES 64
#define UI_MENU_NAME_BYTES 31
#define UI_FONT_TEXT_BYTES 1024
#define WS_SUPPORTED_CHROME \
  (WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_BORDER))

typedef struct ClassRecord {
  BOOL used;
  ATOM atom;
  char name[UI_CLASS_NAME_BYTES + 1];
  WNDPROC procedure;
  uint32_t background;
  UINT style;
  int wndExtra;
  HICON icon;
  HCURSOR cursor;
  HINSTANCE instance;
  uintptr_t menuId; /* integer menu resource, or 0 */
  char menuName[UI_MENU_NAME_BYTES + 1];
} ClassRecord;

typedef struct WindowRecord {
  BOOL used;
  BOOL alive;
  BOOL destroying;
  BOOL recoveryOnly;
  BOOL createSent; /* WM_CREATE was sent by CreateWindowEx */
  BOOL uiRecovery;
  BOOL hasCleanupFont;
  UNDOS_FONT cleanupFont;
  uint32_t active;
  uint64_t identity;
  uint32_t handle;
  uint32_t parent;
  DWORD setupError;
  DWORD setupStatus;
  DWORD cleanupError;
  DWORD cleanupStatus;
  LONG_PTR userData;
  DWORD style;
  WNDPROC procedure;
  UNDOS_UI_CONTEXT *uiContext;
  UINT id;
  BOOL disabled;
  char title[UI_TITLE_BYTES + 1];
  int extraBytes;
  unsigned char extra[UI_WND_EXTRA_BYTES];
} WindowRecord;

/* Style bits a WS_CHILD window may carry on top of the native styles:
   class-specific bits plus the dialog and scroll bar flags. */
#define WS_CHILD_EXTRA                                                   \
  (0x0000FFFFu | WS_GROUP | WS_TABSTOP | WS_DISABLED | WS_VSCROLL |    \
   WS_HSCROLL | WS_CLIPSIBLINGS | WS_CLIPCHILDREN)

typedef struct DCRecord {
  BOOL used;
  BOOL ended;
  uint32_t handle;
  uint32_t window;
  UNDOS_FONT currentFont;
  BOOL hasFont;
  UNDOS_FONT defaultFont;
  BOOL hasDefaultFont;
  COLORREF textColor;
  COLORREF backgroundColor;
  BOOL opaque;
  uintptr_t pen;
  uintptr_t brush;
  uintptr_t font;
  LONG currentX;
  LONG currentY;
  BOOL memory;
  BOOL windowDC; /* from GetDC; released with ReleaseDC */
  uintptr_t bitmap;
  uintptr_t defaultBitmap;
} DCRecord;

extern ClassRecord classes[UI_CLASS_LIMIT];
extern WindowRecord windows[UI_WINDOW_LIMIT];
extern DCRecord dcs[UI_DC_LIMIT];
extern DWORD lastError;
extern DWORD lastStatus;
extern BOOL quitPending;
extern int quitCode;
extern ATOM nextAtom;
extern uint64_t nextWindowIdentity;
extern BYTE keyState[256];

DWORD error_for_status(uint32_t status);
void failure(uint32_t status);
void local_failure(uint32_t status, DWORD error);
BOOL backend_ok(uint32_t status);
BOOL utf8_string(const char *text, size_t limit, size_t *length,
                        BOOL allowEmpty);
ClassRecord *class_by_name(const char *name);
ClassRecord *class_slot(void);
WindowRecord *live_window_by_handle(uint32_t handle);
WindowRecord *window_by_handle(uint32_t handle);
WindowRecord *window_slot(void);
DCRecord *dc_by_handle(uint32_t handle);
DCRecord *dc_slot(void);
uint32_t hwnd_value(HWND window);
uint32_t hdc_value(HDC dc);
HWND hwnd_from_value(uint32_t window);
HDC hdc_from_value(uint32_t dc);
uint32_t style_to_native(DWORD style);
DWORD style_from_native(uint32_t style);
BOOL valid_window_style(DWORD style);
BOOL translate_show(int command, uint32_t *native);
BOOL backend_dc_colors(DCRecord *dc);
void forget_window(WindowRecord *window);
BOOL cleanup_window_dcs(uint32_t handle);
void forget_window_dcs(uint32_t handle);
void forget_descendants(uint32_t parent);
void copy_message(MSG *destination, const UiMessage *source);
void note_key(const MSG *message);
void quit_message(MSG *message);
LRESULT call_window_procedure(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam);
uint32_t nearest_palette(COLORREF color);
BOOL valid_color(COLORREF color);
COLORREF set_dc_color(HDC handle, COLORREF color, BOOL foreground);

void ui_gdi_reset(void);
void ui_bitmap_reset(void);
void ui_resource_reset(void);
/* Bitmap handles for SelectObject/DeleteObject in gdi.c. */
BOOL ui_bitmap_is_handle(uintptr_t handle);
HGDIOBJ ui_bitmap_select(DCRecord *record, uintptr_t handle);
BOOL ui_bitmap_delete(uintptr_t handle);
HBITMAP ui_bitmap_from_dib(const BITMAPINFO *info, const void *bits);

/* Controls (the controls directory) and dialogs (dialog.c). */
extern uint32_t ui_focus_window;
const char *ui_controls_class_name(const char *name);
void ui_controls_attach(HWND window, const char *text);
BOOL ui_controls_is_control(uint32_t handle);
void ui_controls_forget(uint32_t handle);
void ui_controls_reset(void);
void ui_dialog_forget(uint32_t handle);
void ui_dialog_reset(void);
BOOL ui_dialog_window_long(uint32_t handle, int index, BOOL set,
                           LONG_PTR value, LONG_PTR *previous);
void ui_input_reset(void);
void ui_file_reset(void);
void ui_shell_reset(void);
void ui_input_forget(uint32_t handle);
BOOL ui_timer_active(void);
uint32_t ui_timer_wait_ms(void);
BOOL ui_timer_message(MSG *message, BOOL remove);
BOOL ui_timer_dispatch(const MSG *message, LRESULT *result);
void ui_mdi_attach(HWND window, const void *parameter);
void ui_mdi_forget(uint32_t handle);
void ui_mdi_reset(void);
void ui_mdi_retitle(uint32_t handle);
void ui_copy_text(char *destination, size_t capacity, const char *source);

#endif
