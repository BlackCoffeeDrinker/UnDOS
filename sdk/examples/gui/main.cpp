#include <undosgui/gui.hpp>

namespace {
bool running = true;
void procedure(const Msg &message) {
  if (message.message == WindowMessage_Close) running = false;
  if (message.message == WindowMessage_Paint) {
    gui::Paint paint(message.hwnd);
    paint.text(12, 12, "Hello from the UnDOS SDK!");
  }
}
}

int main() {
  if (!gui::register_class("SDKHello", procedure)) return 1;
  const auto window = gui::window("SDKHello", "SDK Hello", 40, 40, 320, 160);
  if (!window) return 1;
  Msg message{};
  while (running && gui::next(message)) gui::dispatch(message);
  return gui::destroy(window) ? 0 : 1;
}
