#include <userspace.h>
#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/stdio.h>
#include <undoscrt/string.h>

#include <limits.h>

namespace {
UNDOS_UI_CONTEXT ui{};
HWND hwnd{};
BOOL running = TRUE;
char notes[512]{};
int argumentCount{};
char **arguments{};
PAINTSTRUCT paint{};
BOOL paintActive{};
UNDOS_FONT previousFont{};
BOOL hasPreviousFont{};

void report(const char *operation, DWORD error, DWORD status) {
  crt_printf("demo: %s failed (error=%u status=%u)\n", operation,
             static_cast<unsigned>(error), static_cast<unsigned>(status));
  (void)UD_SYS_PrintV1("demo: GUI operation failed");
}

void report_last(const char *operation) {
  report(operation, GetLastError(), UndosGetLastStatus());
}

BOOL finish_paint() {
  if (paintActive) {
    if (!EndPaint(hwnd, &paint)) {
      report_last("EndPaint");
      return FALSE;
    }
    paintActive = FALSE;
  }
  if (hasPreviousFont) {
    if (!UndosCloseFont(previousFont)) {
      report_last("UndosCloseFont");
      return FALSE;
    }
    hasPreviousFont = FALSE;
  }
  return TRUE;
}

HDC begin_paint() {
  if ((paintActive || hasPreviousFont) && !finish_paint()) return nullptr;
  paint = {};
  paint.hdc = BeginPaint(hwnd, &paint);
  if (!paint.hdc) {
    report_last("BeginPaint");
    return nullptr;
  }
  paintActive = TRUE;
  BOOL hadPrevious = FALSE;
  if (!UndosSelectFont(paint.hdc, ui.font, &previousFont, &hadPrevious)) {
    report_last("UndosSelectFont");
    return nullptr;
  }
  hasPreviousFont = hadPrevious;
  if (hadPrevious && !UndosCloseFont(previousFont)) {
    report_last("UndosCloseFont");
    return nullptr;
  }
  hasPreviousFont = FALSE;
  return paint.hdc;
}

BOOL fill(HDC dc, int left, int top, int right, int bottom, UINT color) {
  COLORREF value = UndosPaletteColor(color);
  return value != CLR_INVALID &&
      SetTextColor(dc, value) != CLR_INVALID &&
      SetBkColor(dc, value) != CLR_INVALID && SetBkMode(dc, OPAQUE) != 0 &&
      UndosFillRectangle(dc, left, top, right, bottom);
}

BOOL draw(HDC dc, int x, int y, const char *text, UINT foreground = 0,
          UINT background = 7, BOOL opaque = TRUE,
          enum UndosTextLayout layout = UNDOS_TEXT_PROPORTIONAL) {
  COLORREF fg = UndosPaletteColor(foreground);
  COLORREF bg = UndosPaletteColor(background);
  return fg != CLR_INVALID && bg != CLR_INVALID &&
      SetTextColor(dc, fg) != CLR_INVALID && SetBkColor(dc, bg) != CLR_INVALID &&
      SetBkMode(dc, opaque ? OPAQUE : TRANSPARENT) != 0 &&
      UndosDrawText(dc, x, y, UNDOS_TEXT_REGULAR, layout,
                    UNDOS_TEXT_BASIC, text);
}

int button_height(const char *label) {
  int height = 0;
  if (!UndosGetButtonHeight(ui.font, label, &height)) {
    report_last("UndosGetButtonHeight");
    return 0;
  }
  return height;
}

void recover_ui() {
  for (UINT attempt = 0; attempt < 64; ++attempt) {
    UNDOS_WINDOW_CREATION_RECOVERY creation{};
    if (UndosGetWindowCreationRecovery(&creation)) {
      if (!DestroyWindow(creation.window)) {
        report("creation recovery cleanup", GetLastError(),
               UndosGetLastStatus());
        return;
      }
      continue;
    }
    UNDOS_UI_CLEANUP_RECOVERY cleanup{};
    if (UndosGetUiCleanupRecovery(&cleanup)) {
      if (!DestroyWindow(cleanup.window)) {
        report("UI recovery cleanup", GetLastError(), UndosGetLastStatus());
        return;
      }
      continue;
    }
    break;
  }
}

void check_dispatch_result(uint32_t before) {
  if (ui.resultSerial != before && ui.result < 0)
    report("default UI operation", ui.operationError, ui.operationStatus);
  recover_ui();
}

void show_about() {
  const int result = UndosRunDialog(&ui, hwnd, "About",
      "Original UnDOS GUI exerciser. Notes are not saved.", nullptr, 0, FALSE);
  if (result < 0)
    report("About dialog", ui.operationError, ui.operationStatus);
  recover_ui();
}

void copy_scalar_prefix(char *output, size_t capacity, const char *input) {
  size_t used = 0;
  while (input[used] && used + 1 < capacity) {
    const unsigned char lead = static_cast<unsigned char>(input[used]);
    size_t count = lead < 0x80 ? 1 :
        (lead & 0xE0) == 0xC0 ? 2 :
        (lead & 0xF0) == 0xE0 ? 3 :
        (lead & 0xF8) == 0xF0 ? 4 : 0;
    if (!count || used + count >= capacity) break;
    bool complete = true;
    for (size_t i = 1; i < count; ++i)
      if (!input[used + i] ||
          (static_cast<unsigned char>(input[used + i]) & 0xC0) != 0x80)
        complete = false;
    if (!complete) break;
    used += count;
  }
  memcpy(output, input, used);
  output[used] = 0;
}

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam,
                           LPARAM lParam) {
  if (message == WM_CLOSE) {
    running = FALSE;
    return 0;
  }
  if (message == WM_DESTROY) {
    running = FALSE;
    return 0;
  }
  if (message == WM_PAINT) {
    HDC dc = begin_paint();
    if (!dc) return 0;
    RECT area{};
    if (!GetClientRect(window, &area)) {
      report_last("GetClientRect");
      (void)finish_paint();
      return 0;
    }
    const UNDOS_TEXT_METRICS metrics = ui.metrics;
    const int lineHeight = static_cast<int>(metrics.lineHeight);
    const int lineStep = lineHeight + 4;
    (void)draw(dc, 10, 8, "Native UnDOS application");
    (void)draw(dc, 10, 8 + lineStep, "Arguments received:");
    int y = 8 + 2 * lineStep;
    for (int i = 0; i < argumentCount && i < 4; ++i) {
      char line[48]{};
      copy_scalar_prefix(line, sizeof(line), arguments[i]);
      (void)draw(dc, 16, y, line);
      y += lineStep;
    }
    (void)draw(dc, 10, y + 4, "Type a note (not saved):");
    y += lineStep + 4;
    const int clearHeight = button_height("Clear");
    const int aboutHeight = button_height("About");
    const int buttonHeight = clearHeight > aboutHeight
        ? clearHeight : aboutHeight;
    const int buttonY = area.bottom - buttonHeight - 6;
    (void)fill(dc, 8, y, area.right - 8, buttonY - 4, 15);
    const int columns = metrics.cellWidth
        ? (area.right - 24) / static_cast<int>(metrics.cellWidth) : 0;
    if (columns > 0) {
      size_t offset = 0;
      const size_t length = strlen(notes);
      while (offset < length && y + lineHeight <= buttonY - 4) {
        char line[80]{};
        size_t count = length - offset;
        if (count > static_cast<size_t>(columns)) count = columns;
        if (count > sizeof(line) - 1) count = sizeof(line) - 1;
        copy_scalar_prefix(line, sizeof(line), notes + offset);
        const size_t copied = strlen(line);
        if (!copied) break;
        (void)draw(dc, 12, y, line, 0, 15, TRUE, UNDOS_TEXT_UNIFORM_GRID);
        offset += copied;
        y += lineHeight;
      }
    }
    (void)UndosDrawButton(dc, ui.font, 10, buttonY, 80, "Clear", FALSE);
    (void)UndosDrawButton(dc, ui.font, 106, buttonY, 80, "About", FALSE);
    (void)finish_paint();
    return 0;
  }
  if (message == WM_CHAR) {
    size_t length = strlen(notes);
    if (wParam == 8 && length) {
      do { --length; }
      while (length &&
          (static_cast<unsigned char>(notes[length]) & 0xC0) == 0x80);
      notes[length] = 0;
    } else if (wParam >= 32 && wParam < 127 &&
               length + 1 < sizeof(notes)) {
      notes[length] = static_cast<char>(wParam);
      notes[length + 1] = 0;
    }
    (void)InvalidateRect(window, nullptr, TRUE);
    return 0;
  }
  if (message == WM_KEYDOWN && wParam == 112) {
    show_about();
    return 0;
  }
  if (message == WM_SIZE || message == WM_FONTCHANGED ||
      message == WM_DISPLAYCHANGE) {
    if (message == WM_FONTCHANGED &&
        (!ui.metricsValid || wParam != ui.metrics.generation) &&
        !UndosRefreshUi(&ui))
      report("font refresh", ui.operationError, ui.operationStatus);
    if (message == WM_DISPLAYCHANGE) {
      UINT width = 0, height = 0;
      if (!UndosGetDisplaySize(&ui, &width, &height) || !width || !height) {
        report_last("display refresh");
        running = FALSE;
        return 0;
      }
    }
    (void)InvalidateRect(window, nullptr, TRUE);
    return 0;
  }
  if (message == WM_LBUTTONDOWN) {
    RECT area{};
    if (!GetClientRect(window, &area)) {
      report_last("GetClientRect");
      return 0;
    }
    const int x = static_cast<int16_t>(lParam & 0xFFFF);
    const int y = static_cast<int16_t>(lParam >> 16);
    const int clearHeight = button_height("Clear");
    const int aboutHeight = button_height("About");
    const int height = clearHeight > aboutHeight ? clearHeight : aboutHeight;
    const int buttonY = area.bottom - height - 6;
    if (x >= 10 && x < 90 && y >= buttonY && y < buttonY + height) {
      notes[0] = 0;
      (void)InvalidateRect(window, nullptr, TRUE);
    } else if (x >= 106 && x < 186 && y >= buttonY &&
               y < buttonY + height) {
      show_about();
    }
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}
}

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
    (void)UD_SYS_PrintV1(trace);
  }
  if (!UndosInitUi(&ui)) {
    report("UI initialization", ui.operationError, ui.operationStatus);
    (void)UndosCloseUi(&ui);
    return 1;
  }
  UINT displayWidth = 0, displayHeight = 0;
  if (!UndosGetDisplaySize(&ui, &displayWidth, &displayHeight) ||
      displayWidth <= 90 || displayHeight <= 60) {
    report_last("display size");
    (void)UndosCloseUi(&ui);
    return 1;
  }
  const int clearHeight = button_height("Clear");
  const int aboutHeight = button_height("About");
  const uint32_t buttonHeight = static_cast<uint32_t>(
      clearHeight > aboutHeight ? clearHeight : aboutHeight);
  const uint64_t requestedHeight =
      15ull * ui.metrics.lineHeight + buttonHeight + 37;
  const int height = static_cast<int>(requestedHeight < displayHeight - 60
      ? requestedHeight : displayHeight - 60);
  const int width = displayWidth - 90 < 430
      ? static_cast<int>(displayWidth - 90) : 430;
  WNDCLASS klass{};
  klass.lpfnWndProc = procedure;
  klass.hbrBackground = UNDOS_PALETTE_BRUSH(7);
  klass.lpszClassName = "Demo";
  if (!RegisterClass(&klass)) {
    report_last("RegisterClass");
    (void)UndosCloseUi(&ui);
    return 1;
  }
  hwnd = CreateWindow("Demo", "UnDOS Notes - session only",
      WS_VISIBLE | WS_CAPTION | WS_BORDER, 90, 60, width, height,
      nullptr, nullptr, nullptr, nullptr);
  if (!hwnd) {
    report_last("CreateWindow");
    recover_ui();
    (void)UndosCloseUi(&ui);
    return 1;
  }
  if (!UndosAttachUiContext(hwnd, &ui) || !UndosSetFocus(hwnd)) {
    report_last("attach/focus window");
    (void)DestroyWindow(hwnd);
    recover_ui();
    (void)UndosCloseUi(&ui);
    return 1;
  }
  MSG message{};
  while (running) {
    const int received = GetMessage(&message, nullptr, 0, 0);
    if (received < 0) {
      report_last("GetMessage");
      break;
    }
    if (!received) break;
    const uint32_t serial = ui.resultSerial;
    (void)DispatchMessage(&message);
    check_dispatch_result(serial);
  }
  if (!finish_paint()) {
    report_last("paint cleanup");
  } else if (hwnd && !DestroyWindow(hwnd)) {
    report_last("DestroyWindow");
  }
  recover_ui();
  if (!UndosCloseUi(&ui)) report("UndosCloseUi", ui.operationError,
                                 ui.operationStatus);
  (void)UD_SYS_PrintV1("demo: session ended");
  return 0;
}
