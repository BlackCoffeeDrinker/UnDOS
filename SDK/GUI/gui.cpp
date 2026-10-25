#include <undosgui/gui.hpp>
#include <undoscrt/stdio.h>
#include <undoscrt/string.h>

namespace gui {
namespace {
Font g_systemFont;

void system_menu(uint32_t hwnd, int x, int y);
}

bool check(uint32_t result, const char *operation) {
  if (result == SyscallReturnCode_OKAY) return true;
  crt_printf("GUI: %s failed (%x)\n", operation, static_cast<uint32_t>(result));
  char diagnostic[128] = "GUI: ";
  const size_t length = strlen(operation);
  if (length <= sizeof(diagnostic) - 24) {
    memcpy(diagnostic + 5, operation, length);
    memcpy(diagnostic + 5 + length, " failed (", 9);
    size_t offset = 14 + length;
    for (unsigned i = 0; i < 8; ++i)
      diagnostic[offset++] = "0123456789abcdef"[(result >> ((7 - i) * 4)) & 15];
    diagnostic[offset++] = ')';
    diagnostic[offset] = 0;
    (void) UD_SYS_PrintV1(diagnostic);
  } else {
    (void) UD_SYS_PrintV1("GUI: operation failed (diagnostic name too long)");
  }
  return false;
}

bool register_class(const char *name, Procedure procedure, uint32_t background) {
  return check(UD_SYS_RegisterClassV1(name, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(procedure)),
                                    background), "RegisterClass");
}

uint32_t window(const char *className, const char *title, int x, int y, int width, int height,
                uint32_t parent, uint32_t userData) {
  uint32_t hwnd = 0;
  const uint32_t style = WindowStyle_Visible | WindowStyle_Caption | WindowStyle_Border |
                         (parent ? WindowStyle_Child : 0);
  if (!check(UD_SYS_CreateWindowV1(className, title, static_cast<WindowStyle>(style),
                                  x, y, width, height, parent, userData, &hwnd), "CreateWindow")) return 0;
  return hwnd;
}

void dispatch(const Msg &msg) {
  if (!msg.hwnd) return;
  if (msg.message == WindowMessage_FontChanged) refresh_system_font();
  if (msg.message == WindowMessage_NcLButtonDown && msg.wParam == HitTest_SysMenu) {
    system_menu(msg.hwnd, mouse_x(msg), mouse_y(msg));
    return;
  }
  uint32_t value = 0;
  if (!check(UD_SYS_GetWindowLongPtrV1(msg.hwnd, WindowLongIndex_WndProc, &value), "GetWndProc")) return;
  if (value) reinterpret_cast<Procedure>(static_cast<uintptr_t>(value))(msg);
}

bool next(Msg &msg) {
  for (;;) {
    if (!check(UD_SYS_GetMessageV1(&msg), "GetMessage") || msg.message == WindowMessage_Quit)
      return false;
    if (msg.message != WindowMessage_FontChanged) return true;
    dispatch(msg);
  }
}

bool destroy(uint32_t hwnd) {
  const Msg notification{.hwnd = hwnd, .message = WindowMessage_Destroy, .wParam = 0, .lParam = 0, .time = 0};
  dispatch(notification);
  return check(UD_SYS_DestroyWindowV1(hwnd), "DestroyWindow");
}

bool invalidate(uint32_t hwnd) {
  const Rect rect{};
  return check(UD_SYS_InvalidateRectV1(hwnd, &rect, 1), "InvalidateRect");
}

bool show(uint32_t hwnd, ShowCommand command) {
  return check(UD_SYS_ShowWindowV1(hwnd, command), "ShowWindow");
}

bool move(uint32_t hwnd, int x, int y, int width, int height) {
  return check(UD_SYS_MoveWindowV1(hwnd, x, y, width, height), "MoveWindow");
}

Rect client(uint32_t hwnd) {
  Rect rect{};
  check(UD_SYS_GetClientRectV1(hwnd, &rect), "GetClientRect");
  return rect;
}

uint32_t data(uint32_t hwnd) {
  uint32_t value = 0;
  check(UD_SYS_GetWindowLongPtrV1(hwnd, WindowLongIndex_UserData, &value), "GetUserData");
  return value;
}

int mouse_x(const Msg &msg) { return static_cast<int16_t>(msg.lParam & 0xffff); }
int mouse_y(const Msg &msg) { return static_cast<int16_t>(msg.lParam >> 16); }
bool inside(int x, int y, int l, int t, int r, int b) { return x >= l && y >= t && x < r && y < b; }

Font::~Font() {
  if (valid_) (void)UD_SYS_CloseFontV1(handle_);
}

Font::Font(Font &&other) noexcept
    : handle_(other.handle_), generation_(other.generation_), valid_(other.valid_) {
  other.handle_ = 0;
  other.generation_ = 0;
  other.valid_ = false;
}

Font &Font::operator=(Font &&other) noexcept {
  if (this == &other) return *this;
  if (valid_) (void)UD_SYS_CloseFontV1(handle_);
  handle_ = other.handle_;
  generation_ = other.generation_;
  valid_ = other.valid_;
  other.handle_ = 0;
  other.generation_ = 0;
  other.valid_ = false;
  return *this;
}

Font Font::acquire(FontSource source, GlyphFallback fallback) {
  Font font;
  uint32_t handle = 0, generation = 0;
  if (!check(UD_SYS_AcquireFontV1(static_cast<::FontSource>(source),
                                 static_cast<enum GlyphFallbackPolicy>(fallback),
                                 &handle, &generation), "AcquireFont")) return font;
  font.handle_ = handle;
  font.generation_ = generation;
  font.valid_ = true;
  return font;
}

bool Font::metrics(TextMetrics &result, FontStyle style, TextRepertoire repertoire) const {
  result = {};
  if (!valid_) return check(SyscallReturnCode_INVALID_ARGUMENT, "GetFontMetrics");
  TextMetricsV2 metrics{};
  if (!check(UD_SYS_GetFontMetricsV1(handle_, static_cast<::TextStyle>(style),
                                     static_cast<::TextRepertoire>(repertoire), &metrics),
             "GetFontMetrics")) return false;
  result = {metrics.generation, metrics.ascent, metrics.descent, metrics.lineGap,
            metrics.lineHeight, metrics.baselineOffset, metrics.cellWidth,
            metrics.cellHeight, metrics.inkLeft, metrics.inkTop,
            metrics.inkRight, metrics.inkBottom};
  return true;
}

bool Font::measure(const char *text, TextExtent &result, FontStyle style,
                   TextLayoutMode layout, TextRepertoire repertoire) const {
  result = {};
  if (!valid_ || !text) return check(SyscallReturnCode_INVALID_ARGUMENT, "MeasureText");
  TextExtentV2 extent{};
  if (!check(UD_SYS_MeasureTextV1(handle_, static_cast<::TextStyle>(style),
                                  static_cast<::TextLayoutMode>(layout),
                                  static_cast<::TextRepertoire>(repertoire), text, &extent),
             "MeasureText")) return false;
  result = {extent.advanceWidth, extent.lineHeight, extent.baselineOffset,
            extent.inkLeft, extent.inkTop, extent.inkRight, extent.inkBottom,
            extent.generation};
  return true;
}

bool Font::select(uint32_t dc) const {
  if (!valid_) return check(SyscallReturnCode_INVALID_ARGUMENT, "SelectFont");
  uint32_t previous = 0;
  if (!check(UD_SYS_SelectFontV1(dc, handle_, &previous), "SelectFont")) return false;
  if (previous) check(UD_SYS_CloseFontV1(previous), "Close previous font");
  return true;
}

Font &system_font() {
  if (!g_systemFont) g_systemFont = Font::acquire();
  return g_systemFont;
}

void refresh_system_font() {
  Font updated = Font::acquire();
  if (updated) g_systemFont = static_cast<Font &&>(updated);
}

bool TextLayout::measure(const char *text, TextExtent &extent) const {
  return font_.measure(text, extent, style_, mode_, repertoire_);
}

namespace {
bool continuation(char ch) { return (static_cast<unsigned char>(ch) & 0xc0) == 0x80; }

bool fit_run(const TextLayout &layout, const char *value, char *output, uint32_t capacity,
             uint32_t width, TextExtent &extent, bool tail) {
  extent = {};
  if (!value || !output || capacity < 4 || value == output)
    return check(SyscallReturnCode_INVALID_ARGUMENT, "TextLayout");
  output[0] = 0;
  size_t length = 0;
  while (length <= 1024 && value[length]) ++length;
  if (length > 1024) return check(SyscallReturnCode_INVALID_SIZE, "TextLayout");
  TextExtent measured{};
  if (!layout.measure(value, measured)) return false;
  if (length < capacity && measured.width <= width) {
    memcpy(output, value, length + 1);
    extent = measured;
    return true;
  }
  TextExtent dots{};
  if (!tail && !layout.measure("...", dots)) return false;
  if (!tail && dots.width > width) {
    extent.height = measured.height;
    return true;
  }
  size_t boundaries[1025]{};
  size_t count = 0;
  for (size_t i = 1; i <= length; ++i)
    if (i == length || !continuation(value[i])) boundaries[++count] = i;
  size_t low = 0, high = count;
  while (low < high) {
    const size_t middle = low + (high - low + 1) / 2;
    const size_t start = tail ? boundaries[count - middle] : 0;
    const size_t bytes = tail ? length - start : boundaries[middle];
    const size_t suffix = tail ? 0 : 3;
    if (bytes + suffix >= capacity) { high = middle - 1; continue; }
    memcpy(output, value + start, bytes);
    if (suffix) memcpy(output + bytes, "...", suffix);
    output[bytes + suffix] = 0;
    if (!layout.measure(output, measured)) { output[0] = 0; return false; }
    if (measured.width <= width) low = middle;
    else high = middle - 1;
  }
  const size_t start = tail ? boundaries[count - low] : 0;
  const size_t bytes = tail ? length - start : boundaries[low];
  memcpy(output, value + start, bytes);
  if (!tail) memcpy(output + bytes, "...", 3);
  output[bytes + (tail ? 0 : 3)] = 0;
  if (!layout.measure(output, extent)) { output[0] = 0; return false; }
  return true;
}
}

bool TextLayout::fit_prefix(const char *value, char *output, uint32_t capacity,
                            uint32_t width, TextExtent &extent) const {
  return fit_run(*this, value, output, capacity, width, extent, false);
}

bool TextLayout::fit_tail(const char *value, char *output, uint32_t capacity,
                          uint32_t width, TextExtent &extent) const {
  return fit_run(*this, value, output, capacity, width, extent, true);
}

Paint::Paint(uint32_t hwnd) : hwnd_(hwnd) {
  PaintInfo info{};
  if (!check(UD_SYS_BeginPaintV1(hwnd, &dc_, &info), "BeginPaint")) dc_ = 0;
  else if (!system_font().select(dc_)) {
    (void)UD_SYS_EndPaintV1(hwnd_, dc_);
    dc_ = 0;
  }
}

Paint::~Paint() {
  if (dc_) check(UD_SYS_EndPaintV1(hwnd_, dc_), "EndPaint");
}

void Paint::fill(int l, int t, int r, int b, uint32_t color) {
  if (!dc_ || r <= l || b <= t) return;
  if (check(UD_SYS_SetDrawColorsV1(dc_, color, color, 1), "SetDrawColors"))
    check(UD_SYS_RectangleV1(dc_, l, t, r, b), "Rectangle");
}

bool Paint::draw_text(int x, int y, const char *value, FontStyle style,
                      uint32_t color, uint32_t background, bool opaque,
                      TextLayoutMode layout, TextRepertoire repertoire) {
  if (!dc_) return check(SyscallReturnCode_INVALID_ARGUMENT, "DrawText (paint not active)");
  return check(UD_SYS_SetDrawColorsV1(dc_, color, background, opaque ? 1 : 0), "SetDrawColors") &&
         check(UD_SYS_DrawTextV1(dc_, x, y, static_cast<::TextStyle>(style),
                                 static_cast<::TextLayoutMode>(layout),
                                 static_cast<::TextRepertoire>(repertoire), value), "DrawText");
}

int button_height(const char *label) {
  TextExtent extent{};
  if (!system_font().measure(label, extent) ||
      extent.height > static_cast<uint32_t>(INT32_MAX - 8)) return 0;
  return static_cast<int>(extent.height) + 8;
}

void Paint::button(int x, int y, int width, const char *label, bool focused) {
  TextExtent extent{};
  if (!system_font().measure(label, extent)) return;
  if (extent.height > static_cast<uint32_t>(INT32_MAX - 8)) return;
  const int height = static_cast<int>(extent.height) + 8;
  fill(x, y, x + width, y + height, focused ? Ink : 8);
  fill(x + 1, y + 1, x + width - 1, y + height - 1, Paper);
  fill(x + 3, y + 3, x + width - 2, y + height - 2, Face);
  char fitted[128]{};
  const TextLayout layout(system_font());
  if (!layout.fit_prefix(label, fitted, sizeof(fitted),
                         width > 8 ? static_cast<uint32_t>(width - 8) : 0, extent)) return;
  draw_text(x + (width - static_cast<int>(extent.width)) / 2,
            y + (height - static_cast<int>(extent.height)) / 2, fitted);
}

void Paint::icon(int x, int y, uint32_t kind, bool selected) {
  // Original geometric icons; untouched pixels act as the icon mask.
  const uint32_t color = kind % 3 == 0 ? 1 : kind % 3 == 1 ? 4 : 2;
  (void) selected;
  if (kind % 3 == 0) {
    fill(x + 1, y + 1, x + 31, y + 25, Ink);
    fill(x + 2, y + 2, x + 30, y + 24, 8);
    fill(x + 4, y + 4, x + 28, y + 21, color);
    fill(x + 7, y + 8, x + 10, y + 9, Paper);
    fill(x + 9, y + 9, x + 11, y + 10, Paper);
    fill(x + 7, y + 10, x + 10, y + 11, Paper);
    fill(x + 13, y + 13, x + 20, y + 14, Paper);
    fill(x + 12, y + 25, x + 20, y + 28, Ink);
    fill(x + 5, y + 28, x + 27, y + 31, Ink);
    fill(x + 6, y + 28, x + 26, y + 29, Face);
  } else if (kind % 3 == 1) {
    fill(x + 4, y + 1, x + 28, y + 32, Ink);
    fill(x + 5, y + 2, x + 27, y + 31, Paper);
    fill(x + 5, y + 2, x + 27, y + 7, color);
    for (int line = 0; line < 4; ++line)
      fill(x + 8, y + 11 + line * 4, x + 23 - (line % 2) * 4, y + 12 + line * 4, 1);
  } else {
    fill(x, y + 3, x + 32, y + 29, Ink);
    fill(x + 1, y + 4, x + 31, y + 28, Face);
    fill(x + 2, y + 5, x + 30, y + 10, color);
    fill(x + 3, y + 12, x + 29, y + 26, Paper);
    fill(x + 5, y + 15, x + 9, y + 17, Ink);
    fill(x + 7, y + 17, x + 11, y + 19, Ink);
    fill(x + 5, y + 19, x + 9, y + 21, Ink);
    fill(x + 14, y + 21, x + 22, y + 23, Ink);
  }
}

namespace {
struct SystemMenu {
  uint32_t hwnd{0};
  int rowHeight{0};
  int width{0};
  int visibleRows{0};
  int firstRow{0};
  int screenWidth{0};
  int screenHeight{0};
  int selected{0};
  int result{-1};
  bool done{false};
};
SystemMenu *activeSystemMenu{nullptr};
constexpr const char *systemCommands[] = {"Restore", "Move (arrows)", "Size (arrows)", "Minimize", "Maximize", "Close"};

void system_proc(const Msg &msg) {
  if (!activeSystemMenu || msg.hwnd != activeSystemMenu->hwnd) return;
  auto &state = *activeSystemMenu;
  if (msg.message == WindowMessage_Paint) {
    Paint p(msg.hwnd);
    for (int i = state.firstRow;
         i < 6 && i < state.firstRow + state.visibleRows; ++i) {
      const int y = (i - state.firstRow) * state.rowHeight;
      const uint32_t bg = i == state.selected ? Highlight : Face;
      p.fill(0, y, state.width, y + state.rowHeight, bg);
      p.draw_text(5, y + 2, systemCommands[i], FontStyle::Regular,
                  i == state.selected ? Paper : Ink, bg);
    }
  } else if (msg.message == WindowMessage_FontChanged) {
    TextMetrics metrics{};
    if (!system_font().metrics(metrics) || !metrics.lineHeight ||
        metrics.lineHeight > INT32_MAX - 4) {
      state.done = true;
      return;
    }
    const TextLayout layout(system_font());
    uint32_t width = 0;
    for (const char *command : systemCommands) {
      TextExtent extent{};
      if (!layout.measure(command, extent)) { state.done = true; return; }
      if (extent.width > width) width = extent.width;
    }
    state.rowHeight = static_cast<int>(metrics.lineHeight) + 4;
    state.width = width > static_cast<uint32_t>(INT32_MAX - 12)
        ? state.screenWidth : static_cast<int>(width) + 12;
    if (state.width > state.screenWidth) state.width = state.screenWidth;
    state.visibleRows = (state.screenHeight - 2) / state.rowHeight;
    if (state.visibleRows < 1) state.visibleRows = 1;
    if (state.visibleRows > 6) state.visibleRows = 6;
    if (state.selected < state.firstRow) state.firstRow = state.selected;
    else if (state.selected >= state.firstRow + state.visibleRows)
      state.firstRow = state.selected - state.visibleRows + 1;
    Rect rect{};
    if (check(UD_SYS_GetWindowRectV1(state.hwnd, &rect), "Get system-menu rectangle")) {
      const uint64_t logicalHeight = static_cast<uint64_t>(state.rowHeight) *
                                     state.visibleRows + 2;
      const int height = static_cast<int>(logicalHeight < static_cast<uint64_t>(state.screenHeight)
          ? logicalHeight : static_cast<uint64_t>(state.screenHeight));
      int x = rect.left, y = rect.top;
      if (x + state.width > state.screenWidth) x = state.screenWidth - state.width;
      if (y + height > state.screenHeight) y = state.screenHeight - height;
      (void)move(state.hwnd, x, y, state.width, height);
    }
    invalidate(msg.hwnd);
  } else if (msg.message == WindowMessage_Close ||
             (msg.message == WindowMessage_Activate && !msg.wParam)) state.done = true;
  else if (msg.message == WindowMessage_KeyDown) {
    if (msg.wParam == 27) state.done = true;
    if (msg.wParam == 38 || msg.wParam == 40) {
      state.selected = (state.selected + (msg.wParam == 38 ? 5 : 1)) % 6;
      if (state.selected < state.firstRow) state.firstRow = state.selected;
      else if (state.selected >= state.firstRow + state.visibleRows)
        state.firstRow = state.selected - state.visibleRows + 1;
      invalidate(msg.hwnd);
    }
    if (msg.wParam == 13) { state.result = state.selected; state.done = true; }
  } else if (msg.message == WindowMessage_LButtonDown) {
    const int row = state.rowHeight ? state.firstRow + mouse_y(msg) / state.rowHeight : -1;
    if (row >= 0 && row < 6) { state.result = row; state.done = true; }
  }
}

void keyboard_geometry(uint32_t hwnd, bool size) {
  if (!show(hwnd, ShowCommand_Restore)) return;
  Rect original{};
  if (!check(UD_SYS_GetWindowRectV1(hwnd, &original), "GetWindowRect")) return;
  uint32_t parent = 0;
  if (!check(UD_SYS_GetWindowLongPtrV1(hwnd, WindowLongIndex_Parent, &parent), "GetParent")) return;
  int ox = 0, oy = 0;
  if (parent) {
    Rect outer{};
    if (!check(UD_SYS_GetWindowRectV1(parent, &outer), "Get parent rectangle")) return;
    const Rect area = client(parent);
    const int border = (outer.right - outer.left - area.right) / 2;
    ox = outer.left + border;
    oy = outer.bottom - border - area.bottom;
  }
  Rect nextRect = original;
  bool done = false;
  Msg msg{};
  while (!done && next(msg)) {
    if (msg.message == WindowMessage_Paint) dispatch(msg);
    else if (msg.hwnd == hwnd && msg.message == WindowMessage_KeyDown) {
      if (msg.wParam == 13) done = true;
      else if (msg.wParam == 27) {
        move(hwnd, original.left - ox, original.top - oy,
             original.right - original.left, original.bottom - original.top);
        done = true;
      } else {
        int dx = 0, dy = 0;
        if (msg.wParam == 37) dx = -8;
        if (msg.wParam == 39) dx = 8;
        if (msg.wParam == 38) dy = -8;
        if (msg.wParam == 40) dy = 8;
        if (!dx && !dy) continue;
        if (size) {
          if (nextRect.right + dx - nextRect.left >= 150) nextRect.right += dx;
          if (nextRect.bottom + dy - nextRect.top >= 100) nextRect.bottom += dy;
        } else {
          nextRect.left += dx; nextRect.right += dx;
          nextRect.top += dy; nextRect.bottom += dy;
        }
        if (!move(hwnd, nextRect.left - ox, nextRect.top - oy,
                   nextRect.right - nextRect.left, nextRect.bottom - nextRect.top)) done = true;
      }
    }
  }
}

void system_menu(uint32_t hwnd, int x, int y) {
  if (activeSystemMenu || !register_class("GuiSystem", system_proc)) return;
  uint32_t dc = 0;
  DeviceCaps caps{};
  if (!check(UD_SYS_CreateDCV1("", "", DCInitFlags_Primary, &dc), "Query screen")) return;
  const bool queried = check(UD_SYS_GetDeviceCapsV1(dc, &caps), "GetDeviceCaps");
  check(UD_SYS_DeleteDCV1(dc), "Delete query DC");
  if (!queried || !caps.width || !caps.height ||
      caps.width > INT32_MAX || caps.height > INT32_MAX) return;
  SystemMenu state{.screenWidth = static_cast<int>(caps.width),
                   .screenHeight = static_cast<int>(caps.height)};
  TextMetrics metrics{};
  if (!system_font().metrics(metrics) || !metrics.lineHeight ||
      metrics.lineHeight > INT32_MAX - 4) return;
  const TextLayout layout(system_font());
  uint32_t menuWidth = 0;
  for (const char *command : systemCommands) {
    TextExtent extent{};
    if (!layout.measure(command, extent)) return;
    if (extent.width > menuWidth) menuWidth = extent.width;
  }
  state.width = menuWidth > static_cast<uint32_t>(INT32_MAX - 12)
      ? state.screenWidth : static_cast<int>(menuWidth) + 12;
  if (state.width > state.screenWidth) state.width = state.screenWidth;
  state.rowHeight = static_cast<int>(metrics.lineHeight) + 4;
  state.visibleRows = (state.screenHeight - 2) / state.rowHeight;
  if (state.visibleRows < 1) state.visibleRows = 1;
  if (state.visibleRows > 6) state.visibleRows = 6;
  const uint64_t logicalHeight = static_cast<uint64_t>(state.rowHeight) *
                                 state.visibleRows + 2;
  const int height = static_cast<int>(logicalHeight < static_cast<uint64_t>(state.screenHeight)
      ? logicalHeight : static_cast<uint64_t>(state.screenHeight));
  if (x + state.width > state.screenWidth) x = state.screenWidth - state.width;
  if (y + height > state.screenHeight) y = state.screenHeight - height;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (!check(UD_SYS_CreateWindowV1("GuiSystem", "", static_cast<WindowStyle>(WindowStyle_Visible | WindowStyle_Border),
                                  x, y, state.width, height, 0, 0, &state.hwnd), "Create system menu")) return;
  activeSystemMenu = &state;
  check(UD_SYS_SetFocusV1(state.hwnd), "Focus system menu");
  Msg msg{};
  while (!state.done && next(msg)) {
    if (msg.hwnd == state.hwnd) system_proc(msg);
    else if (msg.message == WindowMessage_KeyDown || msg.message == WindowMessage_Char) {
      msg.hwnd = state.hwnd;
      system_proc(msg);
    }
    else if (msg.message == WindowMessage_Paint) dispatch(msg);
  }
  destroy(state.hwnd);
  activeSystemMenu = nullptr;
  check(UD_SYS_SetFocusV1(hwnd), "Restore system-menu focus");
  switch (state.result) {
    case 0: show(hwnd, ShowCommand_Restore); break;
    case 1: keyboard_geometry(hwnd, false); break;
    case 2: keyboard_geometry(hwnd, true); break;
    case 3: show(hwnd, ShowCommand_Minimize); break;
    case 4: show(hwnd, ShowCommand_Maximize); break;
    case 5: {
      const Msg close{.hwnd = hwnd, .message = WindowMessage_Close, .wParam = 0, .lParam = 0, .time = 0};
      dispatch(close);
      break;
    }
    default: break;
  }
}

struct DialogState {
  uint32_t hwnd{0};
  const char *message{nullptr};
  Field *fields{nullptr};
  uint32_t count{0};
  uint32_t focus{0};
  bool done{false};
  bool accepted{false};
  bool confirm{true};
  const char *error{nullptr};
  int lineHeight{0};
  int labelHeight{0};
  int fieldHeight{0};
  int messageLines{0};
  int fieldTop{0};
  int rowPitch{0};
  int buttonY{0};
  int buttonHeight{0};
  int logicalWidth{432};
  int scrollX{0};
  int scrollY{0};
  int viewportWidth{0};
  int viewportHeight{0};
  int maximumWindowHeight{0};
};
DialogState *activeDialog{nullptr};

int dialog_message_lines(const char *text, Paint *paint, int top, int lineHeight,
                         int left = 12, uint32_t *maximumWidth = nullptr) {
  if (!text || !*text) return 0;
  const TextLayout layout(system_font());
  char line[1025]{};
  uint16_t boundaries[1025]{};
  int rows = 0;
  auto emit = [&](size_t bytes, const TextExtent &extent) {
    line[bytes] = 0;
    if (maximumWidth && extent.width > *maximumWidth)
      *maximumWidth = extent.width;
    if (paint) paint->draw_text(left, top + rows * lineHeight, line);
    ++rows;
  };
  while (*text) {
    if (*text == '\n') {
      line[0] = 0;
      TextExtent extent{};
      (void)layout.measure(line, extent);
      emit(0, extent);
      ++text;
      continue;
    }
    size_t count = 0;
    size_t bytes = 0;
    while (text[bytes] && text[bytes] != '\n' &&
           bytes < sizeof(line) - 1 && count < 1024) {
      size_t characterBytes = 1;
      const auto lead = static_cast<unsigned char>(text[bytes]);
      if ((lead & 0xe0) == 0xc0) characterBytes = 2;
      else if ((lead & 0xf0) == 0xe0) characterBytes = 3;
      else if ((lead & 0xf8) == 0xf0) characterBytes = 4;
      for (size_t i = 1; i < characterBytes; ++i) {
        if (!text[bytes + i] || text[bytes + i] == '\n') {
          characterBytes = 1;
          break;
        }
      }
      if (bytes + characterBytes >= sizeof(line)) break;
      bytes += characterBytes;
      boundaries[++count] = static_cast<uint16_t>(bytes);
    }
    if (!count) break;

    size_t low = 0, high = count;
    TextExtent bestExtent{};
    while (low < high) {
      const size_t middle = low + (high - low + 1) / 2;
      const size_t candidateBytes = boundaries[middle];
      memcpy(line, text, candidateBytes);
      line[candidateBytes] = 0;
      TextExtent extent{};
      if (!layout.measure(line, extent)) {
        high = middle - 1;
      } else if (extent.width <= 412) {
        low = middle;
        bestExtent = extent;
      } else {
        high = middle - 1;
      }
    }
    if (!low) low = 1;
    const size_t lineBytes = boundaries[low];
    memcpy(line, text, lineBytes);
    line[lineBytes] = 0;
    if (!layout.measure(line, bestExtent)) bestExtent = {};
    emit(lineBytes, bestExtent);
    text += lineBytes;
  }
  return rows;
}

bool update_dialog_layout(DialogState &dialog) {
  TextMetrics metrics{};
  if (!system_font().metrics(metrics) || !metrics.lineHeight ||
      metrics.lineHeight > INT32_MAX - 8) return false;
  dialog.lineHeight = static_cast<int>(metrics.lineHeight);
  dialog.labelHeight = dialog.lineHeight;
  uint32_t contentWidth = 0;
  for (uint32_t i = 0; i < dialog.count; ++i) {
    TextExtent extent{};
    if (!system_font().measure(dialog.fields[i].label, extent) ||
        extent.height > static_cast<uint32_t>(INT32_MAX - 8)) return false;
    if (static_cast<int>(extent.height) > dialog.labelHeight)
      dialog.labelHeight = static_cast<int>(extent.height);
    if (extent.width > contentWidth) contentWidth = extent.width;
  }
  dialog.fieldHeight = dialog.lineHeight + 8;
  dialog.messageLines = dialog_message_lines(dialog.message, nullptr, 0,
                                               dialog.lineHeight, 12,
                                               &contentWidth);
  if (contentWidth > static_cast<uint32_t>(INT32_MAX - 24)) return false;
  dialog.logicalWidth = contentWidth + 24 > 432
      ? static_cast<int>(contentWidth + 24) : 432;
  dialog.fieldTop = 10 + dialog.messageLines * dialog.lineHeight +
                    (dialog.messageLines ? 8 : 0);
  dialog.rowPitch = dialog.labelHeight + dialog.fieldHeight + 6;
  dialog.buttonHeight = button_height("OK");
  if (dialog.confirm) {
    const int cancelHeight = button_height("Cancel");
    if (cancelHeight > dialog.buttonHeight) dialog.buttonHeight = cancelHeight;
  }
  if (dialog.buttonHeight <= 0) return false;
  dialog.buttonY = dialog.fieldTop + dialog.rowPitch * static_cast<int>(dialog.count);
  return true;
}

int dialog_height(const DialogState &dialog) {
  return dialog.buttonY + dialog.buttonHeight + dialog.lineHeight + 18;
}

void dialog_clamp_scroll(DialogState &dialog) {
  const Rect area = client(dialog.hwnd);
  dialog.viewportWidth = area.right;
  dialog.viewportHeight = area.bottom;
  const int maximumX = dialog.logicalWidth > dialog.viewportWidth
      ? dialog.logicalWidth - dialog.viewportWidth : 0;
  const int maximum = dialog_height(dialog) > dialog.viewportHeight
      ? dialog_height(dialog) - dialog.viewportHeight : 0;
  if (dialog.scrollX < 0) dialog.scrollX = 0;
  if (dialog.scrollX > maximumX) dialog.scrollX = maximumX;
  if (dialog.scrollY < 0) dialog.scrollY = 0;
  if (dialog.scrollY > maximum) dialog.scrollY = maximum;
}

void dialog_reveal_focus(DialogState &dialog) {
  if (dialog.viewportHeight <= 0 || dialog.viewportWidth <= 0) return;
  int left = 10;
  int right = dialog.logicalWidth - 10;
  int top = dialog.buttonY;
  int bottom = dialog.buttonY + dialog.buttonHeight;
  if (dialog.focus < dialog.count) {
    top = dialog.fieldTop + static_cast<int>(dialog.focus) * dialog.rowPitch;
    bottom = top + dialog.rowPitch;
  } else {
    left = 120;
    right = dialog.confirm ? 304 : 200;
  }
  if (left < dialog.scrollX) dialog.scrollX = left;
  else if (right > dialog.scrollX + dialog.viewportWidth)
    dialog.scrollX = right - dialog.viewportWidth;
  if (top < dialog.scrollY) dialog.scrollY = top;
  else if (bottom > dialog.scrollY + dialog.viewportHeight)
    dialog.scrollY = bottom - dialog.viewportHeight;
  dialog_clamp_scroll(dialog);
}

void dialog_paint(DialogState &d) {
  Paint p(d.hwnd);
  if (!p) return;
  (void)dialog_message_lines(d.message, &p, 10 - d.scrollY, d.lineHeight,
                             12 - d.scrollX);
  const TextLayout layout(system_font());
  for (uint32_t i = 0; i < d.count; ++i) {
    const int y = d.fieldTop + static_cast<int>(i) * d.rowPitch - d.scrollY;
    (void)p.draw_text(12 - d.scrollX, y, d.fields[i].label);
    const int fieldTop = y + d.labelHeight + 4;
    p.fill(10 - d.scrollX, fieldTop, d.logicalWidth - 10 - d.scrollX,
           fieldTop + d.fieldHeight, d.focus == i ? Highlight : 8);
    p.fill(12 - d.scrollX, fieldTop + 2, d.logicalWidth - 12 - d.scrollX,
           fieldTop + d.fieldHeight - 2, Paper);
    char fitted[1025]{};
    TextExtent extent{};
    if (layout.fit_tail(d.fields[i].value, fitted, sizeof(fitted), 402, extent))
      p.draw_text(14 - d.scrollX, fieldTop + 4, fitted, FontStyle::Regular, Ink, Paper);
  }
  const int y = d.buttonY - d.scrollY;
  p.button(120 - d.scrollX, y, 80, "OK", d.focus == d.count);
  if (d.confirm) p.button(224 - d.scrollX, y, 80, "Cancel", d.focus == d.count + 1);
  if (d.error)
    p.draw_text(12 - d.scrollX, y + d.buttonHeight + 4,
                d.error, FontStyle::Regular, 4);
}

void dialog_proc(const Msg &msg) {
  if (!activeDialog || msg.hwnd != activeDialog->hwnd) return;
  auto &d = *activeDialog;
  if (msg.message == WindowMessage_Paint) {
    dialog_paint(d);
  }
  else if (msg.message == WindowMessage_Close) d.done = true;
  else if (msg.message == WindowMessage_FontChanged) {
    if (update_dialog_layout(d)) {
      Rect rect{};
      if (check(UD_SYS_GetWindowRectV1(d.hwnd, &rect), "GetWindowRect"))
        move(d.hwnd, rect.left, rect.top, rect.right - rect.left,
             dialog_height(d) < d.maximumWindowHeight
                 ? dialog_height(d) : d.maximumWindowHeight);
      dialog_clamp_scroll(d);
      dialog_reveal_focus(d);
      invalidate(d.hwnd);
    }
  } else if (msg.message == WindowMessage_Size) {
    dialog_clamp_scroll(d);
    dialog_reveal_focus(d);
  }
  else if (msg.message == WindowMessage_KeyDown) {
    if (msg.wParam == 27) d.done = true;
    else if (msg.wParam == 33 || msg.wParam == 34 ||
             msg.wParam == 36 || msg.wParam == 35) {
      if (msg.wParam == 36) d.scrollY = 0;
      else if (msg.wParam == 35) d.scrollY = dialog_height(d);
      else {
        const int step = d.viewportHeight > d.lineHeight
            ? d.viewportHeight - d.lineHeight : 1;
        d.scrollY += msg.wParam == 34 ? step : -step;
      }
      dialog_clamp_scroll(d);
      invalidate(d.hwnd);
    } else if (msg.wParam == 37 || msg.wParam == 39) {
      const int step = d.lineHeight > 0 ? d.lineHeight : 1;
      d.scrollX += msg.wParam == 39 ? step : -step;
      dialog_clamp_scroll(d);
      invalidate(d.hwnd);
    } else if (msg.wParam == 9) {
      const uint32_t controls = d.count + (d.confirm ? 2 : 1);
      const bool reverse = ((msg.lParam >> 16) & 3) != 0;
      d.focus = (d.focus + (reverse ? controls - 1 : 1)) % controls;
      dialog_reveal_focus(d);
      invalidate(d.hwnd);
    } else if (msg.wParam == 13 || (msg.wParam == 32 && d.focus >= d.count)) {
      d.accepted = d.focus != d.count + 1;
      d.done = true;
    }
  } else if (msg.message == WindowMessage_Char && d.focus < d.count) {
    const Field &f = d.fields[d.focus];
    const size_t n = strlen(f.value);
    d.error = nullptr;
    if (msg.wParam == 8 && n) f.value[n - 1] = 0;
    else if (msg.wParam >= 32 && msg.wParam < 127) {
      if (n + 1 < f.capacity) {
        f.value[n] = static_cast<char>(msg.wParam);
        f.value[n + 1] = 0;
      } else d.error = "Field is full";
    }
    invalidate(d.hwnd);
  } else if (msg.message == WindowMessage_LButtonDown) {
    const int x = mouse_x(msg) + d.scrollX, y = mouse_y(msg) + d.scrollY;
    for (uint32_t i = 0; i < d.count; ++i)
      if (inside(x, y, 10, d.fieldTop + static_cast<int>(i) * d.rowPitch,
                 d.logicalWidth - 10,
                 d.fieldTop + static_cast<int>(i) * d.rowPitch +
                          d.rowPitch - 6)) d.focus = i;
    const int by = d.buttonY;
    if (inside(x, y, 120, by, 200, by + d.buttonHeight)) {
      d.accepted = true;
      d.done = true;
    }
    if (d.confirm && inside(x, y, 224, by, 304, by + d.buttonHeight)) d.done = true;
    dialog_reveal_focus(d);
    invalidate(d.hwnd);
  }
}
} // namespace

bool dialog(const char *title, const char *text, Field *fields, uint32_t count, bool confirm) {
  if (activeDialog || count > 4 || (count && !fields)) {
    crt_puts("GUI: unsupported nested/oversized dialog\n");
    return false;
  }
  if (!register_class("GuiDialog", dialog_proc)) return false;
  DialogState state{};
  state.message = text;
  state.fields = fields;
  state.count = count;
  state.confirm = confirm;
  if (!update_dialog_layout(state)) return false;
  uint32_t display = 0;
  DeviceCaps caps{};
  if (!check(UD_SYS_CreateDCV1("", "", DCInitFlags_Primary, &display),
             "Query dialog display")) return false;
  const bool queried = check(UD_SYS_GetDeviceCapsV1(display, &caps),
                             "Get dialog display caps");
  check(UD_SYS_DeleteDCV1(display), "Delete dialog display query");
  if (!queried || !caps.width || !caps.height ||
      caps.width > INT32_MAX || caps.height > INT32_MAX) return false;
  const int maximumWidth = caps.width > 16 ? static_cast<int>(caps.width - 16)
                                          : static_cast<int>(caps.width);
  state.maximumWindowHeight = caps.height > 48
      ? static_cast<int>(caps.height - 48) : static_cast<int>(caps.height);
  const int width = maximumWidth < 440 ? maximumWidth : 440;
  const int height = dialog_height(state) < state.maximumWindowHeight
      ? dialog_height(state) : state.maximumWindowHeight;
  if (width <= 0 || height <= 0) return false;
  const int x = (static_cast<int>(caps.width) - width) / 2;
  const int y = static_cast<int>(caps.height) > height + 24
      ? (static_cast<int>(caps.height) - height) / 2 : 0;
  state.hwnd = window("GuiDialog", title, x, y, width, height);
  if (!state.hwnd) return false;
  activeDialog = &state;
  dialog_clamp_scroll(state);
  dialog_reveal_focus(state);
  check(UD_SYS_SetFocusV1(state.hwnd), "SetFocus");
  invalidate(state.hwnd);
  Msg msg{};
  while (!state.done && next(msg)) {
    if (msg.hwnd == state.hwnd) dialog_proc(msg);
    else if (msg.message == WindowMessage_KeyDown || msg.message == WindowMessage_Char) {
      msg.hwnd = state.hwnd;
      dialog_proc(msg);
    }
    else if (msg.message == WindowMessage_Paint) dispatch(msg);
    else if (msg.message == WindowMessage_LButtonDown ||
             (msg.message == WindowMessage_Activate && msg.wParam))
      check(UD_SYS_SetFocusV1(state.hwnd), "Restore dialog focus");
  }
  destroy(state.hwnd);
  activeDialog = nullptr;
  return state.accepted;
}

void message(const char *title, const char *text) {
  (void) dialog(title, text, nullptr, 0, false);
}
} // namespace gui
