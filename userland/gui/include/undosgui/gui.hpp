#pragma once

#include <userspace.h>
#include <undos/syscalls.h>

namespace gui {

using Procedure = void (*)(const Msg &);
constexpr uint32_t Face = 7;
constexpr uint32_t Ink = 0;
constexpr uint32_t Paper = 15;
constexpr uint32_t Highlight = 1;
constexpr int CellWidth = 8;
constexpr int CellHeight = 16;

bool check(uint32_t result, const char *operation);
bool register_class(const char *name, Procedure procedure, uint32_t background = Face);
uint32_t window(const char *className, const char *title, int x, int y, int width, int height,
                uint32_t parent = 0, uint32_t data = 0);
void dispatch(const Msg &message);
bool next(Msg &message);
bool destroy(uint32_t hwnd);
bool invalidate(uint32_t hwnd);
bool show(uint32_t hwnd, ShowCommand command = ShowCommand_Show);
bool move(uint32_t hwnd, int x, int y, int width, int height);
Rect client(uint32_t hwnd);
uint32_t data(uint32_t hwnd);
int mouse_x(const Msg &message);
int mouse_y(const Msg &message);
bool inside(int x, int y, int left, int top, int right, int bottom);

class Paint {
public:
  explicit Paint(uint32_t hwnd);
  ~Paint();
  Paint(const Paint &) = delete;
  Paint &operator=(const Paint &) = delete;
  explicit operator bool() const { return dc_ != 0; }
  void fill(int left, int top, int right, int bottom, uint32_t color);
  void text(int x, int y, const char *text, uint32_t color = Ink, uint32_t background = Face);
  void button(int x, int y, int width, const char *label, bool focused = false);
  void icon(int x, int y, uint32_t kind, bool selected = false);

private:
  uint32_t hwnd_;
  uint32_t dc_{0};
};

struct Field {
  const char *label;
  char *value;
  uint32_t capacity;
};

// Dialogs are process-local modal: non-dialog messages are dispatched only for painting.
bool dialog(const char *title, const char *message, Field *fields, uint32_t count,
            bool confirm = true);
void message(const char *title, const char *text);

} // namespace gui
