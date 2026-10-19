#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <undoscrt/heap.h>
#include <undoscrt/process.h>
#include <userspace.h>
#include <undos/syscalls.h>
#include <undosgui/gui.hpp>

namespace {

constexpr uint16_t TERM_ROWS = 23;
constexpr uint16_t TERM_COLS = 72;

constexpr int32_t CHAR_WIDTH = 8;
constexpr int32_t CHAR_HEIGHT = 16;
constexpr int32_t TERM_PADDING = 4;

struct TerminalState {
  HANDLE sourceHandle{0};
  HANDLE sinkHandle{0};
  uint32_t hwnd{0};
  bool running{true};
  uint16_t commandLength{0};
  uint32_t modifiers{0};
  HANDLE childHandle{0};
  bool childRunning{false};

  char grid[TERM_ROWS][TERM_COLS]{};
  uint8_t cursorRow{0};
  uint8_t cursorCol{0};

  // ANSI escape sequence parser state
  bool inEscape{false};
  bool inCsi{false};
  char csiParams[32]{};
  size_t csiParamLen{0};

  void init() {
    for (uint16_t r = 0; r < TERM_ROWS; ++r) {
      for (uint16_t c = 0; c < TERM_COLS; ++c) {
        grid[r][c] = ' ';
      }
    }
  }

  void scroll_up() {
    for (uint16_t r = 0; r < TERM_ROWS - 1; ++r) {
      memcpy(grid[r], grid[r + 1], TERM_COLS);
    }
    memset(grid[TERM_ROWS - 1], ' ', TERM_COLS);
  }

  void newline() {
    cursorCol = 0;
    if (cursorRow + 1 < TERM_ROWS) {
      cursorRow++;
    } else {
      scroll_up();
    }
  }

  void handle_csi_command(char cmd) {
    csiParams[csiParamLen] = '\0';
    if (cmd == 'H' || cmd == 'f') {
      int row = 1;
      int col = 1;
      if (csiParamLen > 0) {
        const char *sep = strchr(csiParams, ';');
        if (sep) {
          int r = 0, c = 0;
          for (const char *p = csiParams; p < sep; ++p) r = r * 10 + (*p - '0');
          for (const char *p = sep + 1; *p; ++p) c = c * 10 + (*p - '0');
          if (r > 0) row = r;
          if (c > 0) col = c;
        } else {
          int r = 0;
          for (const char *p = csiParams; *p; ++p) r = r * 10 + (*p - '0');
          if (r > 0) row = r;
        }
      }
      cursorRow = (row > 0 && row <= TERM_ROWS) ? static_cast<uint8_t>(row - 1) : 0;
      cursorCol = (col > 0 && col <= TERM_COLS) ? static_cast<uint8_t>(col - 1) : 0;
    } else if (cmd == 'J') {
      init();
      cursorRow = 0;
      cursorCol = 0;
    } else if (cmd == 'K') {
      for (uint16_t c = cursorCol; c < TERM_COLS; ++c) {
        grid[cursorRow][c] = ' ';
      }
    } else if (cmd == 'A') {
      if (cursorRow > 0) cursorRow--;
    } else if (cmd == 'B') {
      if (cursorRow + 1 < TERM_ROWS) cursorRow++;
    } else if (cmd == 'C') {
      if (cursorCol + 1 < TERM_COLS) cursorCol++;
    } else if (cmd == 'D') {
      if (cursorCol > 0) cursorCol--;
    }
  }

  void put_char(char ch) {
    if (inEscape) {
      if (!inCsi) {
        if (ch == '[') {
          inCsi = true;
          csiParamLen = 0;
          memset(csiParams, 0, sizeof(csiParams));
        } else {
          inEscape = false;
        }
      } else {
        if ((ch >= '0' && ch <= '9') || ch == ';') {
          if (csiParamLen < sizeof(csiParams) - 1) {
            csiParams[csiParamLen++] = ch;
          }
        } else {
          handle_csi_command(ch);
          inEscape = false;
          inCsi = false;
        }
      }
      return;
    }

    if (ch == '\033') {
      inEscape = true;
      inCsi = false;
      return;
    }

    if (ch == '\n') {
      newline();
    } else if (ch == '\r') {
      cursorCol = 0;
    } else if (ch == '\b') {
      if (cursorCol > 0) {
        cursorCol--;
      }
    } else if (ch == '\t') {
      cursorCol = (cursorCol + 8) & ~7;
      if (cursorCol >= TERM_COLS) {
        newline();
      }
    } else if (ch >= 32 && ch <= 126) {
      if (cursorCol >= TERM_COLS) {
        newline();
      }
      grid[cursorRow][cursorCol++] = ch;
    }
  }

  void render() {
    gui::Paint paint(hwnd);
    if (!paint) return;
    const Rect area = gui::client(hwnd);
    paint.fill(0, 0, area.right, area.bottom, 0);

    // Draw text rows
    char lineBuf[TERM_COLS + 1];
    lineBuf[TERM_COLS] = '\0';

    for (uint16_t r = 0; r < TERM_ROWS; ++r) {
      memcpy(lineBuf, grid[r], TERM_COLS);
      int lastNonSpace = TERM_COLS - 1;
      while (lastNonSpace >= 0 && lineBuf[lastNonSpace] == ' ') {
        lastNonSpace--;
      }
      lineBuf[lastNonSpace + 1] = '\0';

      if (lastNonSpace >= 0) {
        int32_t x = TERM_PADDING;
        int32_t y = TERM_PADDING + r * CHAR_HEIGHT;
        paint.text(x, y, lineBuf, 7, 0);
      }
    }
  }
};

static TerminalState s_term;

void drain_output() {
  uint8_t buffer[256];
  bool changed = false;
  for (;;) {
    uint32_t count = sizeof(buffer);
    if (!gui::check(UD_SYS_ReadV1(s_term.sourceHandle, buffer, &count), "Read terminal")) {
      s_term.running = false;
      return;
    }
    if (!count) break;
    changed = true;
    for (uint32_t i = 0; i < count; ++i) s_term.put_char(static_cast<char>(buffer[i]));
  }
  if (changed) gui::invalidate(s_term.hwnd);
}

void output(const char *text) {
  if (!gui::check(UD_SYS_WriteV1(s_term.sinkHandle, reinterpret_cast<const uint8_t *>(text),
                                static_cast<uint32_t>(strlen(text))), "Write terminal"))
    s_term.running = false;
}

void command(char *line) {
  if (char *end = strchr(line, '\n')) *end = 0;
  if (char *end = strchr(line, '\r')) *end = 0;
  if (!strcmp(line, "clear")) {
    s_term.init();
    s_term.cursorRow = s_term.cursorCol = 0;
  } else if (!strcmp(line, "help")) {
    output("help, clear, echo TEXT, run /System/APP.elf [ARGS], exit\n"
           "run connects child stdin/stdout/stderr here until it exits.\n");
  } else if (!strcmp(line, "exit")) s_term.running = false;
  else if (!strncmp(line, "echo ", 5)) { output(line + 5); output("\n"); }
  else if (!strncmp(line, "run ", 4)) {
    char *path = line + 4;
    char *args = strchr(path, ' ');
    if (args) *args++ = 0;
    HANDLE handle = 0;
    if (UD_SYS_CreateProcessV2(path, args ? args : "", ProcessFlags_None,
                              s_term.sinkHandle, s_term.sinkHandle, s_term.sinkHandle,
                              &handle) != SyscallReturnCode_OKAY)
      output("Cannot launch program. Check absolute path and serial log.\n");
    else {
      s_term.childHandle = handle;
      s_term.childRunning = true;
      (void) UD_SYS_PrintV1("term: foreground child started");
    }
  } else if (*line) output("Unknown command. Type help.\n");
  if (s_term.running && !s_term.childRunning) output("> ");
}

void terminal_input(uint8_t ch) {
  if (!s_term.childRunning && (ch >= 32 || ch == '\t') && s_term.commandLength >= 240) {
    gui::message("Command too long", "Commands are limited to 240 characters.");
    gui::check(UD_SYS_SetFocusV1(s_term.hwnd), "SetFocus");
    return;
  }
  if (ch == '\n' || ch == 0x15) s_term.commandLength = 0;
  else if (ch == '\b') { if (s_term.commandLength) --s_term.commandLength; }
  else ++s_term.commandLength;
  if (!gui::check(UD_SYS_WriteV1(s_term.sourceHandle, &ch, 1), "Terminal input")) {
    s_term.running = false;
    return;
  }
  drain_output();
  if (ch == '\n' && !s_term.childRunning) {
    char line[256]{};
    uint32_t n = sizeof(line) - 1;
    if (gui::check(UD_SYS_ReadV1(s_term.sinkHandle, reinterpret_cast<uint8_t *>(line), &n), "Read command")) {
      line[n] = 0;
      command(line);
      drain_output();
    }
  }
}

void procedure(const Msg &msg) {
  if (msg.message == WindowMessage_Close) s_term.running = false;
  else if (msg.message == WindowMessage_Paint) s_term.render();
  else if (msg.message == WindowMessage_Size) gui::invalidate(msg.hwnd);
  else if (msg.message == WindowMessage_KeyDown || msg.message == WindowMessage_KeyUp) {
    s_term.modifiers = msg.lParam >> 16;
    if (msg.message == WindowMessage_KeyUp) return;
    if (msg.wParam == 13) terminal_input('\n');
    else if (msg.wParam == 8) terminal_input('\b');
    else if (msg.wParam == 9) terminal_input('\t');
    else if (msg.wParam == 'U' && (s_term.modifiers & 0x0c)) terminal_input(0x15);
    else if (msg.wParam == 'D' && (s_term.modifiers & 0x0c) && s_term.childRunning) terminal_input(0x04);
  } else if (msg.message == WindowMessage_Char && msg.wParam >= 32 && msg.wParam < 127 &&
             !(s_term.modifiers & 0x3c)) terminal_input(static_cast<uint8_t>(msg.wParam));
}

} // namespace

extern "C" int main(int argc, char **argv) {
  (void) argc;
  (void) argv;

  s_term.init();

  // 1. Allocate virtual TTY pair
  if (UD_SYS_CreateV1(&s_term.sourceHandle, &s_term.sinkHandle) != SyscallReturnCode_OKAY) {
    crt_puts("term: failed to allocate virtual TTY\n");
    return 1;
  }

  if (!gui::check(UD_SYS_SetConfigV1(s_term.sourceHandle,
                            static_cast<TtyModeFlags>(TtyModeFlags_Echo | TtyModeFlags_Canonical | TtyModeFlags_CrToNl | TtyModeFlags_NlToCrNl),
                            TERM_ROWS, TERM_COLS), "Configure TTY")) return 1;

  if (!gui::register_class("Terminal", procedure, 0)) return 1;
  s_term.hwnd = gui::window("Terminal", "UnDOS Terminal", 24, 34,
                            TERM_COLS * CHAR_WIDTH + 2 * TERM_PADDING + 6,
                            TERM_ROWS * CHAR_HEIGHT + 2 * TERM_PADDING + 24);
  if (!s_term.hwnd) return 1;
  gui::check(UD_SYS_SetFocusV1(s_term.hwnd), "SetFocus");

  // Initial banner into the terminal
  output("UnDOS windowed virtual terminal\nType help for commands.\n\n> ");
  drain_output();

  Msg msg{};
  while (s_term.running) {
    if (!s_term.childRunning) {
      if (!gui::next(msg)) break;
      gui::dispatch(msg);
      continue;
    }
    const auto message = UD_SYS_PeekMessageV1(&msg, 1);
    if (message == SyscallReturnCode_OKAY) {
      if (msg.message == WindowMessage_Quit) break;
      gui::dispatch(msg);
    } else if (message != SyscallReturnCode_TIMEOUT) {
      gui::check(message, "PeekMessage");
      break;
    }
    drain_output();
    uint32_t status = 0;
    const auto waited = UD_SYS_WaitProcessV1(s_term.childHandle, 10, &status);
    if (waited == SyscallReturnCode_OKAY) {
      drain_output();
      gui::check(UD_SYS_CloseV1(s_term.childHandle), "Close process");
      s_term.childRunning = false;
      (void) UD_SYS_PrintV1("term: foreground child exited");
      s_term.commandLength = 0;
      output("\n> ");
      drain_output();
    } else if (waited != SyscallReturnCode_TIMEOUT) {
      gui::check(waited, "WaitProcess");
      break;
    }
  }
  if (s_term.childRunning) gui::check(UD_SYS_CloseV1(s_term.childHandle), "Close process");
  gui::destroy(s_term.hwnd);
  gui::check(UD_SYS_CloseV1(s_term.sourceHandle), "Close TTY source");
  gui::check(UD_SYS_CloseV1(s_term.sinkHandle), "Close TTY sink");
  (void) UD_SYS_PrintV1("term: session ended");

  return 0;
}
