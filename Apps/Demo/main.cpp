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
    p.text(10, 8, "Native UnDOS application");
    p.text(10, 30, "Arguments received:");
    int y = 50;
    for (int i = 0; i < argumentCount && i < 4; ++i) {
      char line[48]{};
      const size_t n = strlen(arguments[i]);
      memcpy(line, arguments[i], n > 46 ? 46 : n);
      p.text(16, y, line);
      y += 18;
    }
    p.text(10, y + 8, "Type a note (not saved):");
    y += 30;
    p.fill(8, y, area.right - 8, area.bottom - 38, gui::Paper);
    const int columns = (area.right - 24) / gui::CellWidth;
    if (columns > 0) {
      const size_t length = strlen(notes);
      for (size_t offset = 0; offset < length && y + 16 < area.bottom - 38;) {
        char line[80]{};
        size_t n = length - offset;
        if (n > static_cast<size_t>(columns)) n = static_cast<size_t>(columns);
        if (n > 79) n = 79;
        memcpy(line, notes + offset, n);
        p.text(12, y + 2, line, gui::Ink, gui::Paper);
        offset += n;
        y += 16;
      }
    }
    p.button(10, area.bottom - 30, 80, "Clear");
    p.button(106, area.bottom - 30, 80, "About");
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
  } else if (msg.message == WindowMessage_Size) gui::invalidate(msg.hwnd);
  else if (msg.message == WindowMessage_LButtonDown) {
    const Rect area = gui::client(msg.hwnd);
    const int x = gui::mouse_x(msg), y = gui::mouse_y(msg);
    if (gui::inside(x, y, 10, area.bottom - 30, 90, area.bottom - 6)) {
      notes[0] = 0;
      gui::invalidate(msg.hwnd);
    } else if (gui::inside(x, y, 106, area.bottom - 30, 186, area.bottom - 6)) {
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
  if (!gui::register_class("Demo", procedure)) return 1;
  hwnd = gui::window("Demo", "UnDOS Notes - session only", 90, 60, 430, 345);
  if (!hwnd) return 1;
  gui::check(UD_SYS_SetFocusV1(hwnd), "SetFocus");
  Msg msg{};
  while (running && gui::next(msg)) gui::dispatch(msg);
  gui::destroy(hwnd);
  (void) UD_SYS_PrintV1("demo: session ended");
  return 0;
}
