#include "private/backend.h"
#include "private/font_mapping.h"
#include <userspace.h>

static uint32_t native_window_style(uint32_t style) {
  uint32_t native = 0;
  if (style & UI_WINDOW_VISIBLE) native |= WindowStyle_Visible;
  if (style & UI_WINDOW_CAPTION) native |= WindowStyle_Caption;
  if (style & UI_WINDOW_BORDER) native |= WindowStyle_Border;
  if (style & UI_WINDOW_CHILD) native |= WindowStyle_Child;
  return native;
}

static uint32_t public_window_style(uint32_t native) {
  uint32_t style = 0;
  if (native & WindowStyle_Visible) style |= UI_WINDOW_VISIBLE;
  if (native & WindowStyle_Caption) style |= UI_WINDOW_CAPTION;
  if (native & WindowStyle_Border) style |= UI_WINDOW_BORDER;
  if (native & WindowStyle_Child) style |= UI_WINDOW_CHILD;
  return style;
}

static int native_window_long(uint32_t index, enum WindowLongIndex *native) {
  switch (index) {
    case UI_WINDOW_PROC: *native = WindowLongIndex_WndProc; return 1;
    case UI_WINDOW_USERDATA: *native = WindowLongIndex_UserData; return 1;
    case UI_WINDOW_STYLE: *native = WindowLongIndex_Style; return 1;
    case UI_WINDOW_PARENT: *native = WindowLongIndex_Parent; return 1;
    default: return 0;
  }
}

static int native_show_command(uint32_t command, enum ShowCommand *native) {
  switch (command) {
    case UI_SHOW_HIDE: *native = ShowCommand_Hide; return 1;
    case UI_SHOW_SHOW: *native = ShowCommand_Show; return 1;
    case UI_SHOW_MINIMIZE: *native = ShowCommand_Minimize; return 1;
    case UI_SHOW_MAXIMIZE: *native = ShowCommand_Maximize; return 1;
    case UI_SHOW_RESTORE: *native = ShowCommand_Restore; return 1;
    default: return 0;
  }
}

uint32_t ui_backend_register_class(const char *name, uintptr_t procedure,
                                  uint32_t background, uint32_t *atom) {
  if (procedure > UINT32_MAX) return SyscallReturnCode_INVALID_ARGUMENT;
  *atom = 0;
  return UD_SYS_RegisterClassV1(name, (uint32_t)procedure, background);
}

uint32_t ui_backend_create_window(const char *className, const char *title,
                                  uint32_t style, int32_t x, int32_t y,
                                  int32_t width, int32_t height,
                                  uint32_t parent, uint32_t userData,
                                  uint32_t *window) {
  uint32_t result = 0;
  uint32_t status = UD_SYS_CreateWindowV1(className, title,
      (enum WindowStyle)native_window_style(style), x, y, width, height,
      parent, userData, &result);
  if (status == SyscallReturnCode_OKAY) *window = result;
  return status;
}

uint32_t ui_backend_destroy_window(uint32_t window) {
  return UD_SYS_DestroyWindowV1(window);
}

uint32_t ui_backend_show_window(uint32_t window, uint32_t command) {
  enum ShowCommand native;
  if (!native_show_command(command, &native))
    return SyscallReturnCode_INVALID_ARGUMENT;
  return UD_SYS_ShowWindowV1(window, native);
}

uint32_t ui_backend_move_window(uint32_t window, int32_t x, int32_t y,
                                int32_t width, int32_t height) {
  return UD_SYS_MoveWindowV1(window, x, y, width, height);
}

uint32_t ui_backend_invalidate(uint32_t window, const UiRect *rect,
                               uint32_t erase) {
  struct Rect native = {0};
  if (rect) {
    native.left = rect->left;
    native.top = rect->top;
    native.right = rect->right;
    native.bottom = rect->bottom;
  }
  return UD_SYS_InvalidateRectV2(window, &native, rect == 0, erase);
}

uint32_t ui_backend_get_rect(uint32_t window, uint32_t client, UiRect *rect) {
  struct Rect native = {0};
  uint32_t status = client ? UD_SYS_GetClientRectV1(window, &native)
                           : UD_SYS_GetWindowRectV1(window, &native);
  if (status == SyscallReturnCode_OKAY) {
    rect->left = native.left;
    rect->top = native.top;
    rect->right = native.right;
    rect->bottom = native.bottom;
  }
  return status;
}

uint32_t ui_backend_get_window_long(uint32_t window, uint32_t index,
                                    uint32_t *value) {
  enum WindowLongIndex nativeIndex;
  if (!native_window_long(index, &nativeIndex))
    return SyscallReturnCode_INVALID_ARGUMENT;
  uint32_t result = 0;
  uint32_t status = UD_SYS_GetWindowLongPtrV1(window, nativeIndex, &result);
  if (status == SyscallReturnCode_OKAY) {
    if (index == UI_WINDOW_STYLE) result = public_window_style(result);
    *value = result;
  }
  return status;
}

uint32_t ui_backend_set_window_long(uint32_t window, uint32_t index,
                                    uint32_t value, uint32_t *previous) {
  enum WindowLongIndex nativeIndex;
  if (!native_window_long(index, &nativeIndex))
    return SyscallReturnCode_INVALID_ARGUMENT;
  uint32_t nativeValue = index == UI_WINDOW_STYLE
      ? native_window_style(value) : value;
  uint32_t result = 0;
  uint32_t status = UD_SYS_SetWindowLongPtrV1(window, nativeIndex, nativeValue,
                                               &result);
  if (status == SyscallReturnCode_OKAY) {
    if (index == UI_WINDOW_STYLE) result = public_window_style(result);
    *previous = result;
  }
  return status;
}

uint32_t ui_backend_set_window_text(uint32_t window, const char *text) {
  return UD_SYS_SetWindowTextV1(window, text);
}

uint32_t ui_backend_create_menu(uint32_t popup, uint32_t *menu) {
  uint32_t result = 0;
  uint32_t status = UD_SYS_CreateMenuV1(popup, &result);
  if (status == SyscallReturnCode_OKAY) *menu = result;
  return status;
}

uint32_t ui_backend_destroy_menu(uint32_t menu) {
  return UD_SYS_DestroyMenuV1(menu);
}

uint32_t ui_backend_append_menu(uint32_t menu, uint32_t flags, uint32_t item,
                                const char *text) {
  uint32_t native = MenuItemFlags_String;
  if (flags & UI_MENU_GRAYED) native |= MenuItemFlags_Grayed;
  if (flags & UI_MENU_DISABLED) native |= MenuItemFlags_Disabled;
  if (flags & UI_MENU_POPUP) native |= MenuItemFlags_Popup;
  if (flags & UI_MENU_SEPARATOR) native |= MenuItemFlags_Separator;
  return UD_SYS_AppendMenuV1(menu, (enum MenuItemFlags)native, item, text);
}

uint32_t ui_backend_set_menu(uint32_t window, uint32_t menu) {
  return UD_SYS_SetMenuV1(window, menu);
}

uint32_t ui_backend_get_menu(uint32_t window, uint32_t *menu) {
  uint32_t result = 0;
  uint32_t status = UD_SYS_GetMenuV1(window, &result);
  if (status == SyscallReturnCode_OKAY) *menu = result;
  return status;
}

static void copy_message(UiMessage *to, const struct Msg *from) {
  to->hwnd = from->hwnd;
  to->message = from->message;
  to->wParam = from->wParam;
  to->lParam = from->lParam;
  to->time = from->time;
}

uint32_t ui_backend_get_message(UiMessage *message) {
  struct Msg result = {0};
  uint32_t status = UD_SYS_GetMessageV1(&result);
  if (status == SyscallReturnCode_OKAY) copy_message(message, &result);
  return status;
}

uint32_t ui_backend_peek_message(UiMessage *message, uint32_t remove) {
  struct Msg result = {0};
  uint32_t status = UD_SYS_PeekMessageV1(&result, remove);
  if (status == SyscallReturnCode_OKAY) copy_message(message, &result);
  return status;
}

uint32_t ui_backend_post_message(uint32_t window, uint32_t message,
                                 uint32_t wParam, uint32_t lParam) {
  return UD_SYS_PostMessageV1(window, message, wParam, lParam);
}

uint32_t ui_backend_set_focus(uint32_t window) {
  return UD_SYS_SetFocusV1(window);
}

uint32_t ui_backend_create_display_dc(uint32_t *dc) {
  uint32_t result = 0;
  uint32_t status = UD_SYS_CreateDCV1("", "", DCInitFlags_Primary, &result);
  if (status == SyscallReturnCode_OKAY) *dc = result;
  return status;
}

uint32_t ui_backend_get_display_size(uint32_t dc, uint32_t *width,
                                     uint32_t *height) {
  struct DeviceCaps caps = {0};
  uint32_t status = UD_SYS_GetDeviceCapsV1(dc, &caps);
  if (status == SyscallReturnCode_OKAY) {
    *width = caps.width;
    *height = caps.height;
  }
  return status;
}

uint32_t ui_backend_delete_dc(uint32_t dc) {
  return UD_SYS_DeleteDCV1(dc);
}

uint32_t ui_backend_begin_paint(uint32_t window, uint32_t *dc,
                                UiPaintInfo *paint) {
  uint32_t result = 0;
  struct PaintInfo info = {0};
  uint32_t status = UD_SYS_BeginPaintV1(window, &result, &info);
  if (status == SyscallReturnCode_OKAY) {
    *dc = result;
    paint->rcPaint.left = info.rcPaint.left;
    paint->rcPaint.top = info.rcPaint.top;
    paint->rcPaint.right = info.rcPaint.right;
    paint->rcPaint.bottom = info.rcPaint.bottom;
    paint->erased = info.erased;
  }
  return status;
}

uint32_t ui_backend_end_paint(uint32_t window, uint32_t dc) {
  return UD_SYS_EndPaintV1(window, dc);
}

uint32_t ui_backend_set_draw_colors(uint32_t dc, uint32_t foreground,
                                    uint32_t background, uint32_t opaque) {
  return UD_SYS_SetDrawColorsV1(dc, foreground, background, opaque);
}

uint32_t ui_backend_fill_rectangle(uint32_t dc, int32_t left, int32_t top,
                                   int32_t right, int32_t bottom) {
  return UD_SYS_RectangleV1(dc, left, top, right, bottom);
}

uint32_t ui_backend_move_to(uint32_t dc, int32_t x, int32_t y) {
  return UD_SYS_MoveToV1(dc, x, y);
}

uint32_t ui_backend_line_to(uint32_t dc, int32_t x, int32_t y) {
  return UD_SYS_LineToV1(dc, x, y);
}

uint32_t ui_backend_acquire_font(uint32_t source, uint32_t fallback,
                                 uint32_t *font, uint32_t *generation) {
  uint32_t result = 0, version = 0;
  enum FontSource nativeSource;
  enum GlyphFallbackPolicy nativeFallback;
  if (!ui_map_font_source(source, &nativeSource) ||
      !ui_map_glyph_fallback(fallback, &nativeFallback))
    return SyscallReturnCode_INVALID_ARGUMENT;
  uint32_t status = UD_SYS_AcquireFontV1(nativeSource, nativeFallback,
                                          &result, &version);
  if (status == SyscallReturnCode_OKAY) {
    *font = result;
    *generation = version;
  }
  return status;
}

uint32_t ui_backend_close_font(uint32_t font) {
  return UD_SYS_CloseFontV1(font);
}

uint32_t ui_backend_select_font(uint32_t dc, uint32_t font,
                                uint32_t *previous) {
  uint32_t result = 0;
  uint32_t status = UD_SYS_SelectFontV1(dc, font, &result);
  if (status == SyscallReturnCode_OKAY) *previous = result;
  return status;
}

uint32_t ui_backend_get_font_metrics(uint32_t font, uint32_t style,
                                     uint32_t repertoire, UiTextMetrics *metrics) {
  struct TextMetricsV2 result = {0};
  enum TextStyle nativeStyle;
  enum TextRepertoire nativeRepertoire;
  if (!ui_map_text_style(style, &nativeStyle) ||
      !ui_map_text_repertoire(repertoire, &nativeRepertoire))
    return SyscallReturnCode_INVALID_ARGUMENT;
  uint32_t status = UD_SYS_GetFontMetricsV1(font, nativeStyle,
      nativeRepertoire, &result);
  if (status == SyscallReturnCode_OKAY) {
    metrics->generation = result.generation;
    metrics->ascent = result.ascent;
    metrics->descent = result.descent;
    metrics->lineGap = result.lineGap;
    metrics->lineHeight = result.lineHeight;
    metrics->baselineOffset = result.baselineOffset;
    metrics->cellWidth = result.cellWidth;
    metrics->cellHeight = result.cellHeight;
    metrics->inkLeft = result.inkLeft;
    metrics->inkTop = result.inkTop;
    metrics->inkRight = result.inkRight;
    metrics->inkBottom = result.inkBottom;
  }
  return status;
}

uint32_t ui_backend_measure_text(uint32_t font, uint32_t style, uint32_t layout,
                                 uint32_t repertoire, const char *text,
                                 UiTextExtent *extent) {
  struct TextExtentV2 result = {0};
  enum TextStyle nativeStyle;
  enum TextLayoutMode nativeLayout;
  enum TextRepertoire nativeRepertoire;
  if (!ui_map_text_style(style, &nativeStyle) ||
      !ui_map_text_layout(layout, &nativeLayout) ||
      !ui_map_text_repertoire(repertoire, &nativeRepertoire))
    return SyscallReturnCode_INVALID_ARGUMENT;
  uint32_t status = UD_SYS_MeasureTextV1(font, nativeStyle, nativeLayout,
      nativeRepertoire, text, &result);
  if (status == SyscallReturnCode_OKAY) {
    extent->generation = result.generation;
    extent->advanceWidth = result.advanceWidth;
    extent->lineHeight = result.lineHeight;
    extent->baselineOffset = result.baselineOffset;
    extent->inkLeft = result.inkLeft;
    extent->inkTop = result.inkTop;
    extent->inkRight = result.inkRight;
    extent->inkBottom = result.inkBottom;
  }
  return status;
}

uint32_t ui_backend_draw_text(uint32_t dc, int32_t x, int32_t y,
                              uint32_t style, uint32_t layout,
                              uint32_t repertoire, const char *text) {
  enum TextStyle nativeStyle;
  enum TextLayoutMode nativeLayout;
  enum TextRepertoire nativeRepertoire;
  if (!ui_map_text_style(style, &nativeStyle) ||
      !ui_map_text_layout(layout, &nativeLayout) ||
      !ui_map_text_repertoire(repertoire, &nativeRepertoire))
    return SyscallReturnCode_INVALID_ARGUMENT;
  return UD_SYS_DrawTextV1(dc, x, y, nativeStyle, nativeLayout,
                            nativeRepertoire, text);
}
