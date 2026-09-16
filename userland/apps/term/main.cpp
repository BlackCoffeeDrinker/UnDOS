#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <undoscrt/heap.h>
#include <undoscrt/process.h>
#include <userspace.h>
#include <undos/syscalls.h>

namespace {

constexpr uint16_t TERM_ROWS = 25;
constexpr uint16_t TERM_COLS = 80;

constexpr int32_t CHAR_WIDTH = 8;
constexpr int32_t CHAR_HEIGHT = 16;
constexpr int32_t TERM_PADDING = 4;

struct TerminalState {
  uint32_t sourceHandle{0};
  uint32_t sinkHandle{0};
  uint32_t hdc{0};

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
    if (hdc == 0) return;

    // Clear background
    (void) UD_SYS_RectangleV1(hdc, 0, 0, TERM_COLS * CHAR_WIDTH + 2 * TERM_PADDING,
                              TERM_ROWS * CHAR_HEIGHT + 2 * TERM_PADDING);

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
        (void) UD_SYS_TextOutV1(hdc, x, y, lineBuf);
      }
    }
  }
};

static TerminalState s_term;

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

  // 2. Configure window dimensions (25 rows, 80 cols) and flags (echo, canonical, CRLF)
  (void) UD_SYS_SetConfigV1(s_term.sourceHandle,
                            static_cast<TtyModeFlags>(TtyModeFlags_Echo | TtyModeFlags_Canonical | TtyModeFlags_CrToNl | TtyModeFlags_NlToCrNl),
                            TERM_ROWS, TERM_COLS);

  // 3. Create primary graphics DC
  (void) UD_SYS_CreateDCV1("", "", DCInitFlags_Primary, &s_term.hdc);

  // Initial banner into the terminal
  const char *banner = "\033[2JUnDOS Virtual Terminal (term.elf)\r\n"
                       "Virtual TTY subsystem connected. Type commands below.\r\n\r\n";
  (void) UD_SYS_WriteV1(s_term.sourceHandle, reinterpret_cast<const uint8_t *>(banner), static_cast<uint32_t>(strlen(banner)));

  // 4. Initial render
  s_term.render();

  // 5. Spawn child shell/init if requested or available
  (void) UD_SYS_CreateProcessV1("/System/sh.elf", "", ProcessFlags_None);

  // 6. Loop: read output from child via source and render
  uint8_t buffer[256];
  for (int iter = 0; iter < 100; ++iter) {
    uint32_t nRead = sizeof(buffer);
    if (UD_SYS_ReadV1(s_term.sourceHandle, buffer, &nRead) == SyscallReturnCode_OKAY && nRead > 0) {
      for (uint32_t i = 0; i < nRead; ++i) {
        s_term.put_char(static_cast<char>(buffer[i]));
      }
      s_term.render();
    }
  }

  return 0;
}
