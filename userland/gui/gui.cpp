#include <undosgui/gui.hpp>
#include <undoscrt/stdio.h>
#include <undoscrt/string.h>

namespace gui {
namespace {
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
  if (msg.message == WindowMessage_NcLButtonDown && msg.wParam == HitTest_SysMenu) {
    system_menu(msg.hwnd, mouse_x(msg), mouse_y(msg));
    return;
  }
  uint32_t value = 0;
  if (!check(UD_SYS_GetWindowLongPtrV1(msg.hwnd, WindowLongIndex_WndProc, &value), "GetWndProc")) return;
  if (value) reinterpret_cast<Procedure>(static_cast<uintptr_t>(value))(msg);
}

bool next(Msg &msg) {
  return check(UD_SYS_GetMessageV1(&msg), "GetMessage") && msg.message != WindowMessage_Quit;
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

Paint::Paint(uint32_t hwnd) : hwnd_(hwnd) {
  PaintInfo info{};
  if (!check(UD_SYS_BeginPaintV1(hwnd, &dc_, &info), "BeginPaint")) dc_ = 0;
}

Paint::~Paint() {
  if (dc_) check(UD_SYS_EndPaintV1(hwnd_, dc_), "EndPaint");
}

void Paint::fill(int l, int t, int r, int b, uint32_t color) {
  if (!dc_ || r <= l || b <= t) return;
  if (check(UD_SYS_SetDrawColorsV1(dc_, color, color, 1), "SetDrawColors"))
    check(UD_SYS_RectangleV1(dc_, l, t, r, b), "Rectangle");
}

void Paint::text(int x, int y, const char *value, uint32_t color, uint32_t background) {
  if (!dc_) return;
  if (check(UD_SYS_SetDrawColorsV1(dc_, color, background, 1), "SetDrawColors"))
    check(UD_SYS_TextOutV1(dc_, x, y, value), "TextOut");
}

void Paint::button(int x, int y, int width, const char *label, bool focused) {
  fill(x, y, x + width, y + 24, focused ? Ink : 8);
  fill(x + 1, y + 1, x + width - 1, y + 23, Paper);
  fill(x + 3, y + 3, x + width - 2, y + 22, Face);
  const int labelWidth = static_cast<int>(strlen(label)) * CellWidth;
  text(x + (width - labelWidth) / 2, y + 4, label);
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
    for (int i = 0; i < 6; ++i) {
      const uint32_t bg = i == state.selected ? Highlight : Face;
      p.fill(0, i * 22, 152, i * 22 + 22, bg);
      p.text(5, i * 22 + 3, systemCommands[i], i == state.selected ? Paper : Ink, bg);
    }
  } else if (msg.message == WindowMessage_Close ||
             (msg.message == WindowMessage_Activate && !msg.wParam)) state.done = true;
  else if (msg.message == WindowMessage_KeyDown) {
    if (msg.wParam == 27) state.done = true;
    if (msg.wParam == 38 || msg.wParam == 40) {
      state.selected = (state.selected + (msg.wParam == 38 ? 5 : 1)) % 6;
      invalidate(msg.hwnd);
    }
    if (msg.wParam == 13) { state.result = state.selected; state.done = true; }
  } else if (msg.message == WindowMessage_LButtonDown) {
    const int row = mouse_y(msg) / 22;
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
  if (!queried) return;
  if (x + 160 > static_cast<int>(caps.width)) x = static_cast<int>(caps.width) - 160;
  if (y + 138 > static_cast<int>(caps.height)) y = static_cast<int>(caps.height) - 138;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  SystemMenu state{};
  if (!check(UD_SYS_CreateWindowV1("GuiSystem", "", static_cast<WindowStyle>(WindowStyle_Visible | WindowStyle_Border),
                                  x, y, 160, 138, 0, 0, &state.hwnd), "Create system menu")) return;
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
};
DialogState *activeDialog{nullptr};

void dialog_paint(DialogState &d) {
  Paint p(d.hwnd);
  if (!p) return;
  if (d.message) p.text(12, 10, d.message);
  for (uint32_t i = 0; i < d.count; ++i) {
    const int y = 36 + static_cast<int>(i) * 42;
    p.text(12, y, d.fields[i].label);
    p.fill(10, y + 17, 422, y + 38, d.focus == i ? Highlight : 8);
    p.fill(12, y + 19, 420, y + 36, Paper);
    const char *s = d.fields[i].value;
    const size_t len = strlen(s);
    if (len > 49) s += len - 49;
    p.text(14, y + 19, s, Ink, Paper);
  }
  const int y = 44 + static_cast<int>(d.count) * 42;
  p.button(120, y, 80, "OK", d.focus == d.count);
  if (d.confirm) p.button(224, y, 80, "Cancel", d.focus == d.count + 1);
  if (d.error) p.text(12, y + 29, d.error, 4);
}

void dialog_proc(const Msg &msg) {
  if (!activeDialog || msg.hwnd != activeDialog->hwnd) return;
  auto &d = *activeDialog;
  if (msg.message == WindowMessage_Paint) dialog_paint(d);
  else if (msg.message == WindowMessage_Close) d.done = true;
  else if (msg.message == WindowMessage_KeyDown) {
    if (msg.wParam == 27) d.done = true;
    else if (msg.wParam == 9) {
      const uint32_t controls = d.count + (d.confirm ? 2 : 1);
      const bool reverse = ((msg.lParam >> 16) & 3) != 0;
      d.focus = (d.focus + (reverse ? controls - 1 : 1)) % controls;
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
    const int x = mouse_x(msg), y = mouse_y(msg);
    for (uint32_t i = 0; i < d.count; ++i)
      if (inside(x, y, 10, 53 + static_cast<int>(i) * 42, 422, 74 + static_cast<int>(i) * 42)) d.focus = i;
    const int by = 44 + static_cast<int>(d.count) * 42;
    if (inside(x, y, 120, by, 200, by + 24)) {
      d.accepted = true;
      d.done = true;
    }
    if (d.confirm && inside(x, y, 224, by, 304, by + 24)) d.done = true;
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
  state.hwnd = window("GuiDialog", title, 80, 65, 440, 150 + static_cast<int>(count) * 42);
  if (!state.hwnd) return false;
  activeDialog = &state;
  check(UD_SYS_SetFocusV1(state.hwnd), "SetFocus");
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
