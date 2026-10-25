#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <undoscrt/heap.h>
#include <undoscrt/process.h>
#include <userspace.h>
#include <undos/syscalls.h>
#include <undosgui/gui.hpp>

namespace {

constexpr int32_t TERM_PADDING = 4;
constexpr uint32_t MaxBufferRows = 4096;

struct TerminalState {
  HANDLE sourceHandle{0};
  HANDLE sinkHandle{0};
  uint32_t hwnd{0};
  bool running{true};
  uint16_t commandLength{0};
  uint32_t modifiers{0};
  HANDLE childHandle{0};
  bool childRunning{false};

  char *grid{nullptr};
  char *lineBuffer{nullptr};
  uint32_t bufferRows{0};
  uint16_t columns{0};
  uint16_t visibleRows{0};
  uint16_t visibleColumns{0};
  uint32_t cursorRow{0};
  uint16_t cursorCol{0};
  uint32_t cellWidth{0};
  uint32_t cellHeight{0};
  uint16_t scrollbackOffset{0};

  // ANSI escape sequence parser state
  bool inEscape{false};
  bool inCsi{false};
  char csiParams[32]{};
  size_t csiParamLen{0};

  bool resize_storage(uint32_t rows, uint16_t cols) {
    if (!cols || rows > MaxBufferRows || rows > SIZE_MAX / cols) return false;
    char *replacement = static_cast<char *>(crt_malloc(static_cast<size_t>(rows) * cols));
    if (!replacement) return false;
    char *newLineBuffer = static_cast<char *>(crt_malloc(static_cast<size_t>(cols) + 1));
    if (!newLineBuffer) {
      crt_free(replacement);
      return false;
    }
    memset(replacement, ' ', static_cast<size_t>(rows) * cols);
    const uint32_t copyRows = rows < bufferRows ? rows : bufferRows;
    const uint16_t copyCols = cols < columns ? cols : columns;
    if (grid) {
      for (uint32_t row = 0; row < copyRows; ++row)
        memcpy(replacement + static_cast<size_t>(row) * cols,
               grid + static_cast<size_t>(row) * columns, copyCols);
      crt_free(grid);
    }
    if (lineBuffer) crt_free(lineBuffer);
    grid = replacement;
    lineBuffer = newLineBuffer;
    bufferRows = rows;
    columns = cols;
    if (cursorCol >= columns) cursorCol = columns - 1;
    return true;
  }

  bool ensure_row(uint32_t row) {
    if (row < bufferRows) return true;
    if (row >= MaxBufferRows) return false;
    uint32_t rows = bufferRows ? bufferRows : 1;
    while (rows <= row && rows < MaxBufferRows) {
      const uint32_t doubled = rows > MaxBufferRows / 2 ? MaxBufferRows : rows * 2;
      if (doubled == rows) break;
      rows = doubled;
    }
    return resize_storage(rows, columns);
  }

  bool update_layout() {
    gui::TextMetrics metrics{};
    if (!gui::system_font().metrics(metrics, gui::FontStyle::Regular,
                                    gui::TextRepertoire::Terminal) ||
        !metrics.cellWidth || !metrics.cellHeight) return false;
    const Rect area = gui::client(hwnd);
    const uint32_t width = area.right > 2 * TERM_PADDING
        ? static_cast<uint32_t>(area.right - 2 * TERM_PADDING) : 0;
    const uint32_t height = area.bottom > 2 * TERM_PADDING
        ? static_cast<uint32_t>(area.bottom - 2 * TERM_PADDING) : 0;
    uint32_t cols = width / metrics.cellWidth;
    uint32_t rows = height / metrics.cellHeight;
    if (cols > UINT16_MAX || rows > UINT16_MAX) return false;
    const uint32_t storageRows = rows ? (bufferRows > rows ? bufferRows : rows)
                                      : (bufferRows ? bufferRows : 1);
    const uint16_t storageColumns = static_cast<uint16_t>(
        cols ? cols : (columns ? columns : 1));
    if ((columns != storageColumns || bufferRows < storageRows) &&
        !resize_storage(storageRows, storageColumns)) return false;
    visibleColumns = static_cast<uint16_t>(cols);
    visibleRows = static_cast<uint16_t>(rows);
    cellWidth = metrics.cellWidth;
    cellHeight = metrics.cellHeight;
    if (columns && cursorCol >= columns) cursorCol = columns - 1;
    const uint32_t bottom = cursorRow + 1;
    const uint32_t maximumScroll = bottom > visibleRows ? bottom - visibleRows : 0;
    if (scrollbackOffset > maximumScroll)
      scrollbackOffset = static_cast<uint16_t>(
          maximumScroll > UINT16_MAX ? UINT16_MAX : maximumScroll);
    return gui::check(UD_SYS_SetConfigV1(
        sourceHandle,
        static_cast<TtyModeFlags>(TtyModeFlags_Echo | TtyModeFlags_Canonical |
                                  TtyModeFlags_CrToNl | TtyModeFlags_NlToCrNl),
        visibleRows, visibleColumns), "Resize TTY");
  }

  void init() {
    if (grid) memset(grid, ' ', static_cast<size_t>(bufferRows) * columns);
    cursorRow = cursorCol = 0;
    scrollbackOffset = 0;
  }

  char *row(uint32_t index) {
    return index < bufferRows ? grid + static_cast<size_t>(index) * columns : nullptr;
  }

  void newline() {
    cursorCol = 0;
    if (cursorRow + 1 < MaxBufferRows && ensure_row(cursorRow + 1)) ++cursorRow;
    else if (bufferRows == MaxBufferRows) {
      for (uint32_t rowIndex = 0; rowIndex + 1 < bufferRows; ++rowIndex)
        memcpy(row(rowIndex), row(rowIndex + 1), columns);
      memset(row(bufferRows - 1), ' ', columns);
      cursorRow = bufferRows - 1;
    } else {
      running = false;
    }
    scrollbackOffset = 0;
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
      const uint32_t top = cursorRow + 1 > visibleRows ? cursorRow + 1 - visibleRows : 0;
      cursorRow = top + static_cast<uint32_t>(row - 1);
      if (cursorRow >= MaxBufferRows || !ensure_row(cursorRow)) cursorRow = top;
      cursorCol = col > 0 && col <= columns ? static_cast<uint16_t>(col - 1) : 0;
      scrollbackOffset = 0;
    } else if (cmd == 'J') {
      init();
    } else if (cmd == 'K') {
      if (char *current = row(cursorRow))
        memset(current + cursorCol, ' ', columns - cursorCol);
    } else if (cmd == 'A') {
      if (cursorRow > 0) cursorRow--;
    } else if (cmd == 'B') {
      if (cursorRow + 1 < MaxBufferRows && ensure_row(cursorRow + 1)) cursorRow++;
    } else if (cmd == 'C') {
      if (cursorCol + 1 < columns) cursorCol++;
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
      if (cursorCol >= columns) {
        newline();
      }
    } else if (ch >= 32 && ch <= 126) {
      if (cursorCol >= columns) {
        newline();
      }
      if (ensure_row(cursorRow)) row(cursorRow)[cursorCol++] = ch;
      scrollbackOffset = 0;
    }
  }

  void render() {
    gui::Paint paint(hwnd);
    if (!paint) return;
    const Rect area = gui::client(hwnd);
    paint.fill(0, 0, area.right, area.bottom, 0);
    if (!visibleRows || !visibleColumns || !cellWidth || !cellHeight) return;
    const uint32_t bottom = cursorRow + 1;
    const uint32_t maxTop = bottom > visibleRows ? bottom - visibleRows : 0;
    const uint32_t top = maxTop > scrollbackOffset ? maxTop - scrollbackOffset : 0;
    for (uint16_t screenRow = 0; screenRow < visibleRows; ++screenRow) {
      const uint32_t logicalRow = top + screenRow;
      const char *contents = row(logicalRow);
      if (!contents) continue;
      int last = static_cast<int>(visibleColumns) - 1;
      while (last >= 0 && contents[last] == ' ') --last;
      if (last >= 0) {
        memcpy(lineBuffer, contents, static_cast<size_t>(last + 1));
        lineBuffer[last + 1] = 0;
        paint.draw_text(TERM_PADDING,
                        TERM_PADDING + static_cast<int>(screenRow) * cellHeight,
                        lineBuffer, gui::FontStyle::Regular, gui::Paper, 0, true,
                        gui::TextLayoutMode::UniformGrid,
                        gui::TextRepertoire::Terminal);
      }
    }
    if (!scrollbackOffset && cursorRow >= top && cursorRow < top + visibleRows &&
        cursorCol < visibleColumns) {
      const int x = TERM_PADDING + static_cast<int>(cursorCol) * cellWidth;
      const int y = TERM_PADDING + static_cast<int>(cursorRow - top) * cellHeight;
      paint.fill(x, y, x + cellWidth, y + cellHeight, gui::Paper);
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
    const char *path = line + 4;
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
  else if (msg.message == WindowMessage_Size ||
           msg.message == WindowMessage_FontChanged) {
    if (!s_term.update_layout()) s_term.running = false;
    gui::invalidate(msg.hwnd);
  }
  else if (msg.message == WindowMessage_KeyDown || msg.message == WindowMessage_KeyUp) {
    s_term.modifiers = msg.lParam >> 16;
    if (msg.message == WindowMessage_KeyUp) return;
    if (msg.wParam == 13) terminal_input('\n');
    else if (msg.wParam == 8) terminal_input('\b');
    else if (msg.wParam == 9) terminal_input('\t');
    else if (msg.wParam == 33) {
      const uint32_t top = s_term.cursorRow + 1 > s_term.visibleRows
          ? s_term.cursorRow + 1 - s_term.visibleRows : 0;
      const uint32_t next = s_term.scrollbackOffset + s_term.visibleRows;
      s_term.scrollbackOffset = static_cast<uint16_t>(
          next > top ? top : next);
      gui::invalidate(msg.hwnd);
    } else if (msg.wParam == 34) {
      s_term.scrollbackOffset = s_term.scrollbackOffset > s_term.visibleRows
          ? s_term.scrollbackOffset - s_term.visibleRows : 0;
      gui::invalidate(msg.hwnd);
    }
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

  gui::TextMetrics metrics{};
  if (!gui::system_font().metrics(metrics, gui::FontStyle::Regular,
                                  gui::TextRepertoire::Terminal) ||
      !metrics.cellWidth || !metrics.cellHeight) return 1;
  uint32_t display = 0;
  DeviceCaps caps{};
  if (!gui::check(UD_SYS_CreateDCV1("", "", DCInitFlags_Primary, &display),
                  "Open terminal display")) return 1;
  const bool queried = gui::check(UD_SYS_GetDeviceCapsV1(display, &caps),
                                  "Query terminal display");
  gui::check(UD_SYS_DeleteDCV1(display), "Close terminal display");
  if (!queried || !caps.width || !caps.height) return 1;
  if (!gui::register_class("Terminal", procedure, 0)) return 1;
  const uint64_t requestedWidth = 72ull * metrics.cellWidth + 2 * TERM_PADDING + 6;
  const uint64_t requestedHeight = 23ull * metrics.cellHeight + 2 * TERM_PADDING +
                                  metrics.lineHeight + 12;
  const uint64_t availableWidth = caps.width > 24 ? caps.width - 24 : caps.width;
  const uint64_t availableHeight = caps.height > 34 ? caps.height - 34 : caps.height;
  const uint64_t screenWidth = availableWidth < INT32_MAX ? availableWidth : INT32_MAX;
  const uint64_t screenHeight = availableHeight < INT32_MAX ? availableHeight : INT32_MAX;
  const int32_t width = static_cast<int32_t>(
      requestedWidth < screenWidth ? requestedWidth : screenWidth);
  const int32_t height = static_cast<int32_t>(
      requestedHeight < screenHeight ? requestedHeight : screenHeight);
  s_term.hwnd = gui::window("Terminal", "UnDOS Terminal", 24, 34,
                            width, height);
  if (!s_term.hwnd) return 1;
  if (!s_term.update_layout()) return 1;
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
  if (s_term.grid) crt_free(s_term.grid);
  if (s_term.lineBuffer) crt_free(s_term.lineBuffer);
  gui::check(UD_SYS_CloseV1(s_term.sourceHandle), "Close TTY source");
  gui::check(UD_SYS_CloseV1(s_term.sinkHandle), "Close TTY sink");
  (void) UD_SYS_PrintV1("term: session ended");

  return 0;
}
