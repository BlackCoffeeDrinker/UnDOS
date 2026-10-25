#include <undosgui/gui.hpp>
#include <undoscrt/string.h>
#include <undoscrt/stdio.h>

namespace {
uint32_t hwnd{0};
bool running{true};
char notes[512]{};
int argumentCount{0};
char **arguments{nullptr};

void procedure(const Msg &msg) {
  if (msg.message == WindowMessage_Close) running = false;
  else if (msg.message == WindowMessage_Paint) {
    gui::Paint p(msg.hwnd);
    const Rect area = gui::client(msg.hwnd);
    gui::TextMetrics metrics{};
    if (!gui::system_font().metrics(metrics)) return;
    const int lineHeight = static_cast<int>(metrics.lineHeight);
    const int lineStep = lineHeight + 4;
    p.draw_text(10, 8, "Native UnDOS application");
    p.draw_text(10, 8 + lineStep, "Arguments received:");
    int y = 8 + 2 * lineStep;
    for (int i = 0; i < argumentCount && i < 4; ++i) {
      char line[48]{};
      const size_t n = strlen(arguments[i]);
      memcpy(line, arguments[i], n > 46 ? 46 : n);
      p.draw_text(16, y, line);
      y += lineStep;
    }
    p.draw_text(10, y + 4, "Type a note (not saved):");
    y += lineStep + 4;
    const int buttonHeight = gui::button_height("Clear") > gui::button_height("About")
        ? gui::button_height("Clear") : gui::button_height("About");
    const int buttonY = area.bottom - buttonHeight - 6;
    p.fill(8, y, area.right - 8, buttonY - 4, gui::Paper);
    const int columns = metrics.cellWidth
        ? (area.right - 24) / static_cast<int>(metrics.cellWidth) : 0;
    if (columns > 0) {
      const size_t length = strlen(notes);
      for (size_t offset = 0; offset < length && y + lineHeight <= buttonY - 4;) {
        char line[80]{};
        size_t n = length - offset;
        if (n > static_cast<size_t>(columns)) n = static_cast<size_t>(columns);
        if (n > 79) n = 79;
        memcpy(line, notes + offset, n);
        p.draw_text(12, y, line, gui::FontStyle::Regular, gui::Ink, gui::Paper,
                    true, gui::TextLayoutMode::UniformGrid);
        offset += n;
        y += lineHeight;
      }
    }
    p.button(10, buttonY, 80, "Clear");
    p.button(106, buttonY, 80, "About");
  } else if (msg.message == WindowMessage_Char) {
    const size_t length = strlen(notes);
    if (msg.wParam == 8 && length) notes[length - 1] = 0;
    else if (msg.wParam >= 32 && msg.wParam < 127 && length + 1 < sizeof(notes)) {
      notes[length] = static_cast<char>(msg.wParam);
      notes[length + 1] = 0;
    }
    gui::invalidate(msg.hwnd);
  } else if (msg.message == WindowMessage_KeyDown && msg.wParam == 112) {
    gui::message("About", "Original UnDOS GUI exerciser. Notes are not saved.");
    gui::check(UD_SYS_SetFocusV1(hwnd), "SetFocus");
  } else if (msg.message == WindowMessage_Size ||
             msg.message == WindowMessage_FontChanged) gui::invalidate(msg.hwnd);
  else if (msg.message == WindowMessage_LButtonDown) {
    const Rect area = gui::client(msg.hwnd);
    const int x = gui::mouse_x(msg), y = gui::mouse_y(msg);
    const int buttonHeight = gui::button_height("Clear") > gui::button_height("About")
        ? gui::button_height("Clear") : gui::button_height("About");
    const int buttonY = area.bottom - buttonHeight - 6;
    if (gui::inside(x, y, 10, buttonY, 90, buttonY + buttonHeight)) {
      notes[0] = 0;
      gui::invalidate(msg.hwnd);
    } else if (gui::inside(x, y, 106, buttonY, 186, buttonY + buttonHeight)) {
      gui::message("About", "Original UnDOS GUI exerciser. Notes are not saved.");
      gui::check(UD_SYS_SetFocusV1(hwnd), "SetFocus");
    }
  }
}
} // namespace

extern "C" int main(int argc, char **argv) {
  if (argc < 0 || (argc && !argv)) {
    crt_puts("demo: invalid process startup arguments\n");
    return 1;
  }
  argumentCount = argc;
  arguments = argv;
  for (int i = 0; i < argc; ++i) {
    char trace[1056] = "demo: argv[";
    size_t offset = strlen(trace);
    if (i >= 10) trace[offset++] = static_cast<char>('0' + i / 10);
    trace[offset++] = static_cast<char>('0' + i % 10);
    trace[offset++] = ']';
    trace[offset++] = '=';
    const size_t length = strlen(argv[i]);
    if (length >= sizeof(trace) - offset) return 1;
    memcpy(trace + offset, argv[i], length + 1);
    (void) UD_SYS_PrintV1(trace);
  }
  gui::TextMetrics metrics{};
  if (!gui::system_font().metrics(metrics) || !metrics.lineHeight) return 1;
  uint32_t display = 0;
  DeviceCaps caps{};
  if (!gui::check(UD_SYS_CreateDCV1("", "", DCInitFlags_Primary, &display),
                  "Open demo display")) return 1;
  const bool queried = gui::check(UD_SYS_GetDeviceCapsV1(display, &caps),
                                  "Query demo display");
  gui::check(UD_SYS_DeleteDCV1(display), "Close demo display");
  if (!queried || caps.width <= 90 || caps.height <= 60) return 1;
  const int clearButtonHeight = gui::button_height("Clear");
  const int aboutButtonHeight = gui::button_height("About");
  const uint32_t buttonHeight = static_cast<uint32_t>(
      clearButtonHeight > aboutButtonHeight ? clearButtonHeight : aboutButtonHeight);
  const uint64_t requestedHeight = 15ull * metrics.lineHeight + buttonHeight + 37;
  const int height = static_cast<int>(
      requestedHeight < caps.height - 60 ? requestedHeight : caps.height - 60);
  const int width = caps.width - 90 < 430 ? static_cast<int>(caps.width - 90) : 430;
  if (!gui::register_class("Demo", procedure)) return 1;
  hwnd = gui::window("Demo", "UnDOS Notes - session only", 90, 60, width, height);
  if (!hwnd) return 1;
  gui::check(UD_SYS_SetFocusV1(hwnd), "SetFocus");
  Msg msg{};
  while (running && gui::next(msg)) gui::dispatch(msg);
  gui::destroy(hwnd);
  (void) UD_SYS_PrintV1("demo: session ended");
  return 0;
}
