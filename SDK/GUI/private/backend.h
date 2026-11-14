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
  UI_MENU_CHECKED = 8,
  UI_MENU_POPUP = 16,
  UI_MENU_SEPARATOR = 2048
};

typedef struct UiRect {
  int32_t left, top, right, bottom;
} UiRect;

typedef struct UiMessage {
  uint32_t hwnd, message, wParam, lParam, time;
  // Kernel modifier mask (UNDOS_KEYSTATE_* bits); key messages only.
  uint32_t modifiers;
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
uint32_t ui_backend_modify_menu_state(uint32_t menu, uint32_t item,
                                      uint32_t byPosition, uint32_t mask,
                                      uint32_t flags, uint32_t *previous,
                                      uint32_t *itemId);
uint32_t ui_backend_get_message(UiMessage *message);
uint32_t ui_backend_peek_message(UiMessage *message, uint32_t remove);
/* Waits until a message is queued (not removed); UI_STATUS_TIMEOUT after
   timeoutMs, UINT32_MAX waits forever. */
uint32_t ui_backend_wait_message(uint32_t timeoutMs);
uint32_t ui_backend_post_message(uint32_t window, uint32_t message,
                                 uint32_t wParam, uint32_t lParam);
uint32_t ui_backend_set_focus(uint32_t window);
uint32_t ui_backend_remove_menu_item(uint32_t menu, uint32_t item,
                                     uint32_t byPosition, uint32_t *itemId);
uint32_t ui_backend_set_capture(uint32_t window);
uint32_t ui_backend_release_capture(void);
/* Milliseconds since an arbitrary start; ui_backend_sleep waits up to ms. */
uint32_t ui_backend_tick_ms(void);
void ui_backend_sleep(uint32_t ms);

/* Threads and kernel wait objects (PROC syscalls). */
uint32_t ui_backend_create_thread(uint32_t entry, uint32_t parameter, uint32_t stackSize,
                                  uint32_t *handle, uint32_t *threadId);
__attribute__((noreturn)) void ui_backend_exit_thread(uint32_t code);
uint32_t ui_backend_thread_exit_code(uint32_t handle, uint32_t *code);
uint32_t ui_backend_current_thread_id(void);
uint32_t ui_backend_wait_object(uint32_t handle, uint32_t timeoutMs);
uint32_t ui_backend_create_event(uint32_t manualReset, uint32_t initialState, uint32_t *handle);
uint32_t ui_backend_set_event(uint32_t handle);
uint32_t ui_backend_reset_event(uint32_t handle);
uint32_t ui_backend_close_handle(uint32_t handle);

/* File system (kernel32 layer). Paths are absolute UnDOS paths. */
enum { UI_FILE_COPY, UI_FILE_MOVE, UI_FILE_DELETE, UI_FILE_MKDIR };
uint32_t ui_backend_file_open(const char *path, uint32_t *handle);
uint32_t ui_backend_file_close(uint32_t handle);
/* Times are packed FAT/DOS stamps, (date << 16) | time; 0 when unknown. */
typedef struct UiFileInfo {
  uint64_t size;
  uint32_t attributes, creationTime, lastAccessTime, lastWriteTime;
} UiFileInfo;
/* found=0 at the end; name receives at most capacity bytes incl. NUL. */
uint32_t ui_backend_file_read_directory(uint32_t handle, uint32_t index,
                                        char *name, uint32_t capacity,
                                        UiFileInfo *info, uint32_t *found);
uint32_t ui_backend_file_information(uint32_t handle, UiFileInfo *info);
uint32_t ui_backend_file_operate(uint32_t operation, const char *source,
                                 const char *destination);
uint32_t ui_backend_file_set_attributes(const char *path, uint32_t attributes);
/* found=0 at the end; name receives at most capacity bytes incl. NUL. */
uint32_t ui_backend_query_mount(uint32_t index, char *name, uint32_t capacity,
                                uint32_t *found);
enum { UI_VOLUME_READ_ONLY = 1u, UI_VOLUME_CASE_PRESERVED = 2u };
uint32_t ui_backend_volume_information(const char *mountPoint, char *label,
                                       uint32_t labelCapacity, char *fileSystem,
                                       uint32_t fileSystemCapacity,
                                       uint32_t *serial, uint32_t *maxComponent,
                                       uint32_t *flags);
uint32_t ui_backend_create_process(const char *path, const char *arguments);
uint32_t ui_backend_create_display_dc(uint32_t *dc);
uint32_t ui_backend_get_display_size(uint32_t dc, uint32_t *width,
                                     uint32_t *height);
uint32_t ui_backend_delete_dc(uint32_t dc);
uint32_t ui_backend_begin_paint(uint32_t window, uint32_t *dc,
                                UiPaintInfo *paint);
uint32_t ui_backend_end_paint(uint32_t window, uint32_t dc);
uint32_t ui_backend_get_dc(uint32_t window, uint32_t *dc);
uint32_t ui_backend_release_dc(uint32_t window, uint32_t dc);
uint32_t ui_backend_set_draw_colors(uint32_t dc, uint32_t foreground,
                                    uint32_t background, uint32_t opaque);
uint32_t ui_backend_fill_rectangle(uint32_t dc, int32_t left, int32_t top,
                                   int32_t right, int32_t bottom);
uint32_t ui_backend_move_to(uint32_t dc, int32_t x, int32_t y);
uint32_t ui_backend_line_to(uint32_t dc, int32_t x, int32_t y);
uint32_t ui_backend_create_memory_dc(uint32_t *dc);
/* Pixels are top-down BGRA (ARGB32); pixels may be NULL. */
uint32_t ui_backend_create_bitmap(uint32_t width, uint32_t height,
                                  const uint8_t *pixels, uint32_t length,
                                  uint32_t *bitmap);
uint32_t ui_backend_set_bitmap_bits(uint32_t bitmap, const uint8_t *pixels,
                                    uint32_t length);
uint32_t ui_backend_get_bitmap_bits(uint32_t bitmap, uint8_t *pixels,
                                    uint32_t length);
uint32_t ui_backend_select_bitmap(uint32_t dc, uint32_t bitmap);
uint32_t ui_backend_delete_bitmap(uint32_t bitmap);
uint32_t ui_backend_bit_blt(uint32_t dst, int32_t x, int32_t y, uint32_t width,
                            uint32_t height, uint32_t src, int32_t srcX,
                            int32_t srcY);
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
