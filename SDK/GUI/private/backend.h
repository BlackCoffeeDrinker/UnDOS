#ifndef UNDOSGUI_PRIVATE_BACKEND_H
#define UNDOSGUI_PRIVATE_BACKEND_H

#include <stdint.h>

enum UiStatus {
  UI_STATUS_OK = 0,
  UI_STATUS_UNKNOWN = 0xFFFFFFFFu,
  UI_STATUS_INVALID_SIZE = 0xFFFFFFFEu,
  UI_STATUS_OUT_OF_MEMORY = 0xFFFFFFFDu,
  UI_STATUS_INVALID_ADDRESS = 0xFFFFFFFCu,
  UI_STATUS_INVALID_ARGUMENT = 0xFFFFFFFBu,
  UI_STATUS_UNSUPPORTED = 0xFFFFFFFAu,
  UI_STATUS_TIMEOUT = 0xFFFFFFF9u,
  UI_STATUS_RETRY = 0xFFFFFFF8u,
  UI_STATUS_NOT_FOUND = 0xFFFFFFF7u
};

enum UiWindowStyle {
  UI_WINDOW_VISIBLE = 1u,
  UI_WINDOW_CAPTION = 2u,
  UI_WINDOW_BORDER = 4u,
  UI_WINDOW_CHILD = 8u
};

enum UiWindowLongIndex {
  UI_WINDOW_PROC = 0,
  UI_WINDOW_USERDATA = 1,
  UI_WINDOW_STYLE = 2,
  UI_WINDOW_PARENT = 3
};

enum UiShowCommand {
  UI_SHOW_HIDE = 0,
  UI_SHOW_SHOW = 1,
  UI_SHOW_MINIMIZE = 2,
  UI_SHOW_MAXIMIZE = 3,
  UI_SHOW_RESTORE = 4
};

enum UiMenuFlags {
  UI_MENU_STRING = 0,
  UI_MENU_GRAYED = 1,
  UI_MENU_DISABLED = 2,
  UI_MENU_POPUP = 16,
  UI_MENU_SEPARATOR = 2048
};

typedef struct UiRect {
  int32_t left, top, right, bottom;
} UiRect;

typedef struct UiMessage {
  uint32_t hwnd, message, wParam, lParam, time;
} UiMessage;

typedef struct UiPaintInfo {
  UiRect rcPaint;
  uint32_t erased;
} UiPaintInfo;

typedef struct UiTextMetrics {
  uint32_t generation, ascent, descent, lineGap, lineHeight, baselineOffset;
  uint32_t cellWidth, cellHeight;
  int32_t inkLeft, inkTop, inkRight, inkBottom;
} UiTextMetrics;

typedef struct UiTextExtent {
  uint32_t generation, advanceWidth, lineHeight, baselineOffset;
  int32_t inkLeft, inkTop, inkRight, inkBottom;
} UiTextExtent;

#ifdef __cplusplus
extern "C" {
#endif

uint32_t ui_backend_register_class(const char *name, uintptr_t procedure,
                                  uint32_t background, uint32_t *atom);
uint32_t ui_backend_create_window(const char *className, const char *title,
                                  uint32_t style, int32_t x, int32_t y,
                                  int32_t width, int32_t height,
                                  uint32_t parent, uint32_t userData,
                                  uint32_t *window);
uint32_t ui_backend_destroy_window(uint32_t window);
uint32_t ui_backend_show_window(uint32_t window, uint32_t command);
uint32_t ui_backend_move_window(uint32_t window, int32_t x, int32_t y,
                                int32_t width, int32_t height);
uint32_t ui_backend_invalidate(uint32_t window, const UiRect *rect,
                               uint32_t erase);
uint32_t ui_backend_get_rect(uint32_t window, uint32_t client, UiRect *rect);
uint32_t ui_backend_get_window_long(uint32_t window, uint32_t index,
                                    uint32_t *value);
uint32_t ui_backend_set_window_long(uint32_t window, uint32_t index,
                                    uint32_t value, uint32_t *previous);
uint32_t ui_backend_set_window_text(uint32_t window, const char *text);
uint32_t ui_backend_create_menu(uint32_t popup, uint32_t *menu);
uint32_t ui_backend_destroy_menu(uint32_t menu);
uint32_t ui_backend_append_menu(uint32_t menu, uint32_t flags, uint32_t item,
                                const char *text);
uint32_t ui_backend_set_menu(uint32_t window, uint32_t menu);
uint32_t ui_backend_get_menu(uint32_t window, uint32_t *menu);
uint32_t ui_backend_get_message(UiMessage *message);
uint32_t ui_backend_peek_message(UiMessage *message, uint32_t remove);
uint32_t ui_backend_post_message(uint32_t window, uint32_t message,
                                 uint32_t wParam, uint32_t lParam);
uint32_t ui_backend_set_focus(uint32_t window);
uint32_t ui_backend_create_display_dc(uint32_t *dc);
uint32_t ui_backend_get_display_size(uint32_t dc, uint32_t *width,
                                     uint32_t *height);
uint32_t ui_backend_delete_dc(uint32_t dc);
uint32_t ui_backend_begin_paint(uint32_t window, uint32_t *dc,
                                UiPaintInfo *paint);
uint32_t ui_backend_end_paint(uint32_t window, uint32_t dc);
uint32_t ui_backend_set_draw_colors(uint32_t dc, uint32_t foreground,
                                    uint32_t background, uint32_t opaque);
uint32_t ui_backend_fill_rectangle(uint32_t dc, int32_t left, int32_t top,
                                   int32_t right, int32_t bottom);
uint32_t ui_backend_move_to(uint32_t dc, int32_t x, int32_t y);
uint32_t ui_backend_line_to(uint32_t dc, int32_t x, int32_t y);
uint32_t ui_backend_acquire_font(uint32_t source, uint32_t fallback,
                                 uint32_t *font, uint32_t *generation);
uint32_t ui_backend_close_font(uint32_t font);
uint32_t ui_backend_select_font(uint32_t dc, uint32_t font,
                                uint32_t *previous);
uint32_t ui_backend_get_font_metrics(uint32_t font, uint32_t style,
                                     uint32_t repertoire, UiTextMetrics *metrics);
uint32_t ui_backend_measure_text(uint32_t font, uint32_t style, uint32_t layout,
                                 uint32_t repertoire, const char *text,
                                 UiTextExtent *extent);
uint32_t ui_backend_draw_text(uint32_t dc, int32_t x, int32_t y,
                              uint32_t style, uint32_t layout,
                              uint32_t repertoire, const char *text);

#ifdef __cplusplus
}
#endif

#endif
