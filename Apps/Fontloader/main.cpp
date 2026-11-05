#include "decoder.hpp"
#include "font_load_result.hpp"
#include <undoscrt/heap.h>
#include <undoscrt/process.h>
#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <windows.h>
#include <undosgui/extensions.h>
#include <userspace.h>

namespace {
void lifecycle(const char *message) {
  crt_puts(message);
  (void)UD_SYS_PrintV1(message);
}
}

#if !defined(UNDOS_FONTLOADER_TEST_CALLBACKS_ONLY)
[[noreturn]] void fontloader_allocation_failure() {
  lifecycle("fontloader: allocation failed\n");
  crt_exit(static_cast<int>(fontloader::LoadResult::Fatal));
}
#endif

namespace {
#if !defined(UNDOS_FONTLOADER_TEST_CALLBACKS_ONLY)
fontloader::LoadResult load_face(const char *path, uint32_t &face) {
  using fontloader::LoadResult;
  using namespace undos::font;
  HANDLE fileHandle = 0;
  const auto opened = UD_SYS_OpenV1(path, FileOpenMode_Read, &fileHandle);
  if (opened != SyscallReturnCode_OKAY) {
    crt_printf("fontloader: open %s failed, status %u\n", path, opened);
    (void)UD_SYS_PrintV1(path);
    lifecycle("fontloader: file open failed\n");
    return fontloader::open_result(opened);
  }
  loader::GrowableArray<uint8_t> file;
  uint8_t chunk[4096];
  while (true) {
    uint32_t count = sizeof(chunk);
    const auto status = UD_SYS_ReadV1(fileHandle, chunk, &count);
    if (status != SyscallReturnCode_OKAY || count > sizeof(chunk) ||
        count > MaxFileBytes - file.size()) {
      const auto closed = UD_SYS_CloseV1(fileHandle);
      if (closed != SyscallReturnCode_OKAY) lifecycle("fontloader: source file close failed\n");
      crt_printf("fontloader: read failed or file exceeds 4 MiB, status %u\n", status);
      lifecycle("fontloader: read/size rejected\n");
      return LoadResult::Fatal;
    }
    if (!count) break;
    for (uint32_t i = 0; i < count; ++i) file.push_back(chunk[i]);
  }
  const auto sourceClosed = UD_SYS_CloseV1(fileHandle);
  if (sourceClosed != SyscallReturnCode_OKAY) {
    crt_printf("fontloader: source file close failed, status %u\n", sourceClosed);
    lifecycle("fontloader: source file close failed\n");
    return LoadResult::Fatal;
  }
  lifecycle("fontloader: file read\n");
  loader::GrowableArray<uint8_t> resource;
  const auto decoded = loader::decode({.data = file.data(), .size = file.size()}, resource);
  if (decoded != loader::DecodeError::None) {
    crt_printf("fontloader: decode failed: %s\n", loader::error_message(decoded));
    (void)UD_SYS_PrintV1(loader::error_message(decoded));
    lifecycle("fontloader: decode rejected\n");
    return LoadResult::Fatal;
  }
  const auto validation = validate({.data = resource.data(), .size = resource.size()});
  if (validation != Error::None) {
    crt_printf("fontloader: normalized validation failed: %s\n", error_message(validation));
    lifecycle("fontloader: normalized resource rejected\n");
    return LoadResult::Fatal;
  }
  lifecycle("fontloader: decoded and structurally validated (no execution)\n");
  const auto uploaded = fontloader::upload_resource(resource.data(), uint32_t(resource.size()), face,
      [](uint32_t size, uint32_t &upload) {
        const auto status = UD_SYS_BeginFontUploadV1(size, &upload);
        if (status == SyscallReturnCode_OKAY) lifecycle("fontloader: upload begun\n");
        return status;
      },
      [](uint32_t upload, uint32_t offset, const uint8_t *data, uint32_t size) {
        const auto status = UD_SYS_AppendFontUploadV1(upload, offset, data, size);
        if (status != SyscallReturnCode_OKAY)
          crt_printf("fontloader: append at %u failed, status %u\n", offset, status);
        return status;
      },
      [](uint32_t upload, uint32_t &result, uint32_t &validationError) {
        const auto status = UD_SYS_CommitFontUploadV1(upload, &result, &validationError);
        if (status != SyscallReturnCode_OKAY)
          crt_printf("fontloader: commit failed, status %u, validation %u\n", status, validationError);
        return status;
      },
      [](uint32_t upload) { return UD_SYS_CloseFontResourceV1(upload); },
      [](const char *message, uint32_t status) {
        crt_printf("%s, status %u\n", message, status);
        lifecycle(message);
      });
  if (uploaded == LoadResult::Success) lifecycle("fontload: face committed\n");
  return uploaded;
}

bool parse_ppem(const char *text, uint32_t &ppem) {
  ppem = 0;
  if (!text || !*text) return false;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9' || ppem > 1000) return false;
    ppem = ppem * 10 + uint32_t(*text - '0');
  }
  return true;
}

bool close_face(uint32_t face) {
  const auto status = UD_SYS_CloseFontResourceV1(face);
  if (status == SyscallReturnCode_OKAY) {
    lifecycle("fontloader: face closed\n");
    return true;
  }
  crt_printf("fontloader: face close failed, status %u\n", status);
  lifecycle("fontloader: face close failed\n");
  return false;
}
#endif

struct Preview {
  HWND hwnd{};
  bool running{true};
  bool painted{false};
  bool failed{false};
  UNDOS_UI_CONTEXT ui{};
  UNDOS_TEXT_EXTENT regular{};
  UNDOS_TEXT_EXTENT bold{};
  UNDOS_TEXT_EXTENT transparent{};
  int regularY{0};
  int boldY{0};
  int transparentY{0};
  int width{420};
  int height{0};
  PAINTSTRUCT paint{};
  bool paintActive{false};
  UNDOS_FONT previousFont{};
  bool hasPreviousFont{false};
};
Preview preview;
constexpr const char *PreviewText = "Hello, UnDOS! Wiim 012345";

bool ui_ok(const char *operation) {
  const DWORD error = GetLastError();
  const DWORD status = UndosGetLastStatus();
  crt_printf("fontloader: %s failed, error %u status %u\n", operation,
             error, status);
  lifecycle("fontloader: GUI operation failed\n");
  return false;
}

bool size_client(HWND window, int width, int height) {
  RECT outer{}, inner{};
  if (!GetWindowRect(window, &outer) || !GetClientRect(window, &inner))
    return ui_ok("GetWindowRect/GetClientRect");
  const int64_t frameWidth = static_cast<int64_t>(outer.right) - outer.left -
                             inner.right;
  const int64_t frameHeight = static_cast<int64_t>(outer.bottom) - outer.top -
                              inner.bottom;
  const int64_t outerWidth = width + frameWidth;
  const int64_t outerHeight = height + frameHeight;
  if (frameWidth < 0 || frameHeight < 0 || outerWidth > INT32_MAX ||
      outerHeight > INT32_MAX)
    return false;
  return MoveWindow(window, outer.left, outer.top, static_cast<int>(outerWidth),
                    static_cast<int>(outerHeight), TRUE) != FALSE;
}

bool layout_preview() {
  const UNDOS_TEXT_METRICS &metrics = preview.ui.metrics;
  if (!preview.ui.metricsValid || !metrics.lineHeight ||
      !UndosMeasureText(preview.ui.font, UNDOS_TEXT_REGULAR,
          UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, PreviewText,
          &preview.regular) ||
      !UndosMeasureText(preview.ui.font, UNDOS_TEXT_BOLD,
          UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, PreviewText,
          &preview.bold) ||
      !UndosMeasureText(preview.ui.font, UNDOS_TEXT_REGULAR,
          UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "Transparent text",
          &preview.transparent) ||
      metrics.generation != preview.ui.metrics.generation ||
      preview.regular.generation != metrics.generation ||
      preview.bold.generation != metrics.generation ||
      preview.transparent.generation != metrics.generation)
    return false;
  constexpr int gap = 8;
  constexpr int padding = 12;
  const uint64_t boldY =
      static_cast<uint64_t>(preview.regular.lineHeight) + gap + padding;
  const uint64_t transparentY = boldY + preview.bold.lineHeight + gap;
  const uint32_t transparentHeight =
      preview.transparent.lineHeight > metrics.lineHeight
          ? preview.transparent.lineHeight : metrics.lineHeight;
  const uint64_t contentHeight = transparentY + transparentHeight + padding;
  uint32_t textWidth = preview.regular.advanceWidth;
  if (preview.bold.advanceWidth > textWidth)
    textWidth = preview.bold.advanceWidth;
  if (preview.transparent.advanceWidth > textWidth)
    textWidth = preview.transparent.advanceWidth;
  const uint64_t contentWidth = static_cast<uint64_t>(textWidth) + 2 * padding;
  if (boldY > INT32_MAX || transparentY > INT32_MAX ||
      contentHeight > INT32_MAX || contentWidth > INT32_MAX) return false;
  preview.regularY = padding;
  preview.boldY = static_cast<int>(boldY);
  preview.transparentY = static_cast<int>(transparentY);
  preview.height = static_cast<int>(contentHeight);
  preview.width = static_cast<int>(contentWidth > 420 ? contentWidth : 420);
  return true;
}

bool constrain_preview(UINT displayWidth, UINT displayHeight) {
  if (!displayWidth || !displayHeight) return false;
  const int maximumWidth = static_cast<int>(
      displayWidth < INT32_MAX ? displayWidth : INT32_MAX);
  const int maximumHeight = static_cast<int>(
      displayHeight < INT32_MAX ? displayHeight : INT32_MAX);
  if (preview.width > maximumWidth) preview.width = maximumWidth;
  if (preview.height > maximumHeight) preview.height = maximumHeight;
  return preview.width > 0 && preview.height > 0 &&
      (!preview.hwnd || size_client(preview.hwnd, preview.width,
                                    preview.height));
}

bool finish_preview_paint() {
  if (preview.paintActive) {
    if (!EndPaint(preview.hwnd, &preview.paint)) return ui_ok("EndPaint");
    preview.paintActive = false;
  }
  if (preview.hasPreviousFont) {
    if (!UndosCloseFont(preview.previousFont))
      return ui_ok("UndosCloseFont");
    preview.hasPreviousFont = false;
  }
  return true;
}

bool preview_colors(HDC dc, UINT fg, UINT bg, bool opaque = true) {
  return SetTextColor(dc, UndosPaletteColor(fg)) != CLR_INVALID &&
      SetBkColor(dc, UndosPaletteColor(bg)) != CLR_INVALID &&
      SetBkMode(dc, opaque ? OPAQUE : TRANSPARENT) != 0;
}

bool preview_draw(HDC dc, int x, int y, const char *text, UINT fg, UINT bg,
                  bool opaque = true, UINT style = UNDOS_TEXT_REGULAR) {
  return preview_colors(dc, fg, bg, opaque) &&
      UndosDrawText(dc, x, y, static_cast<UndosTextStyle>(style),
          UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, text);
}

void preview_recover() {
  for (UINT i = 0; i < 64; ++i) {
    UNDOS_WINDOW_CREATION_RECOVERY creation{};
    if (UndosGetWindowCreationRecovery(&creation)) {
      if (!DestroyWindow(creation.window)) {
        crt_printf("fontloader: creation recovery failed %u/%u\n",
                   creation.cleanupError, creation.cleanupStatus);
        return;
      }
      continue;
    }
    UNDOS_UI_CLEANUP_RECOVERY cleanup{};
    if (UndosGetUiCleanupRecovery(&cleanup)) {
      if (!DestroyWindow(cleanup.window)) {
        crt_printf("fontloader: UI recovery failed %u/%u\n",
                   cleanup.cleanupError, cleanup.cleanupStatus);
        return;
      }
      continue;
    }
    break;
  }
}

LRESULT CALLBACK preview_proc(HWND window, UINT message, WPARAM wParam,
                              LPARAM lParam) {
  if (window != preview.hwnd) return DefWindowProc(window, message, wParam, lParam);
  if (message == WM_CLOSE ||
      (message == WM_KEYDOWN && wParam == VK_ESCAPE)) {
    preview.running = false;
    return 0;
  } else if (message == WM_FONTCHANGED) {
    if ((!preview.ui.metricsValid ||
         wParam != preview.ui.metrics.generation) &&
        !UndosRefreshUi(&preview.ui)) {
      preview.failed = true;
      preview.running = false;
      return 0;
    }
    UINT displayWidth = 0, displayHeight = 0;
    if (!layout_preview() ||
        !UndosGetDisplaySize(&preview.ui, &displayWidth, &displayHeight) ||
        !constrain_preview(displayWidth, displayHeight)) {
      preview.failed = true;
      preview.running = false;
      return 0;
    }
    (void)InvalidateRect(window, nullptr, TRUE);
    return 0;
  } else if (message == WM_DISPLAYCHANGE) {
    UINT displayWidth = 0, displayHeight = 0;
    if (!UndosGetDisplaySize(&preview.ui, &displayWidth, &displayHeight) ||
        !layout_preview() ||
        !constrain_preview(displayWidth, displayHeight)) {
      preview.failed = true;
      preview.running = false;
      return 0;
    }
    (void)InvalidateRect(window, nullptr, TRUE);
    return 0;
  } else if (message == WM_SIZE) {
    (void)InvalidateRect(window, nullptr, TRUE);
    return 0;
  } else if (message == WM_PAINT) {
    if ((preview.paintActive || preview.hasPreviousFont) &&
        !finish_preview_paint()) {
      preview.failed = true;
      preview.running = false;
      return 0;
    }
    preview.paint = {};
    preview.paint.hdc = BeginPaint(window, &preview.paint);
    if (!preview.paint.hdc) {
      ui_ok("BeginPaint");
      preview.failed = true;
      preview.running = false;
      return 0;
    }
    preview.paintActive = true;
    BOOL hadPrevious = FALSE;
    if (!UndosSelectFont(preview.paint.hdc, preview.ui.font,
                         &preview.previousFont, &hadPrevious)) {
      ui_ok("UndosSelectFont");
      preview.failed = true;
      preview.running = false;
      return 0;
    }
    preview.hasPreviousFont = hadPrevious;
    if (hadPrevious && !UndosCloseFont(preview.previousFont)) {
      ui_ok("UndosCloseFont");
      preview.failed = true;
      preview.running = false;
      return 0;
    }
    preview.hasPreviousFont = false;
    HDC dc = preview.paint.hdc;
    const bool okay =
        preview_draw(dc, 12, preview.regularY, PreviewText, 0, 15) &&
        preview_draw(dc, 12, preview.boldY, PreviewText, 0, 15, TRUE,
                     UNDOS_TEXT_BOLD);
    if (!okay) {
      ui_ok("preview text");
      lifecycle("fontload: preview FAIL\n");
      preview.failed = true;
      preview.running = false;
      (void)finish_preview_paint();
      return 0;
    }
    if (!preview_colors(dc, 7, 7) ||
        !UndosFillRectangle(dc, 12, preview.transparentY,
            12 + static_cast<int>(preview.transparent.advanceWidth),
            preview.transparentY +
                static_cast<int>(preview.transparent.lineHeight)) ||
        !preview_draw(dc, 12, preview.transparentY, "Transparent text",
                      1, 15, false)) {
      ui_ok("transparent preview text");
      lifecycle("fontload: preview FAIL\n");
      preview.failed = true;
      preview.running = false;
      (void)finish_preview_paint();
      return 0;
    }
    if (!preview_colors(dc, 1, 1) ||
        !UndosFillRectangle(dc, 12,
            preview.regularY + static_cast<int>(preview.regular.lineHeight),
            12 + static_cast<int>(preview.regular.advanceWidth),
            preview.regularY + 1 +
                static_cast<int>(preview.regular.lineHeight)) ||
        !UndosFillRectangle(dc, 12,
            preview.boldY + static_cast<int>(preview.bold.lineHeight),
            12 + static_cast<int>(preview.bold.advanceWidth),
            preview.boldY + 1 + static_cast<int>(preview.bold.lineHeight))) {
      ui_ok("preview marker");
      preview.failed = true;
      preview.running = false;
    }
    if (!finish_preview_paint()) {
      preview.failed = true;
      preview.running = false;
    }
    if (!preview.painted) {
      preview.painted = true;
      lifecycle("fontload: preview painted\n");
    }
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

int show_preview() {
  if (!UndosInitUi(&preview.ui)) {
    (void)UndosCloseUi(&preview.ui);
    return 1;
  }
  UINT displayWidth = 0, displayHeight = 0;
  if (!UndosGetDisplaySize(&preview.ui, &displayWidth, &displayHeight) ||
      !displayWidth || !displayHeight) {
    (void)UndosCloseUi(&preview.ui);
    return 1;
  }
  WNDCLASS klass{};
  klass.lpfnWndProc = preview_proc;
  klass.hbrBackground = UNDOS_PALETTE_BRUSH(15);
  klass.lpszClassName = "FontPreview";
  if (!layout_preview() ||
      !constrain_preview(displayWidth, displayHeight) ||
      !RegisterClass(&klass)) {
    (void)UndosCloseUi(&preview.ui);
    return 1;
  }
  const int x = displayWidth > static_cast<UINT>(preview.width)
      ? (displayWidth - static_cast<UINT>(preview.width) > 160
          ? 160 : static_cast<int>(displayWidth - preview.width)) : 0;
  const int y = displayHeight > static_cast<UINT>(preview.height)
      ? (displayHeight - static_cast<UINT>(preview.height) > 120
          ? 120 : static_cast<int>(displayHeight - preview.height)) : 0;
  preview.hwnd = CreateWindow("FontPreview", "Default font: regular and bold",
      WS_VISIBLE | WS_CAPTION | WS_BORDER, x, y, preview.width,
      preview.height, nullptr, nullptr, nullptr, nullptr);
  if (!preview.hwnd) {
    preview_recover();
    (void)UndosCloseUi(&preview.ui);
    return 1;
  }
  if (!UndosAttachUiContext(preview.hwnd, &preview.ui)) {
    (void)DestroyWindow(preview.hwnd);
    preview_recover();
    (void)UndosCloseUi(&preview.ui);
    return 1;
  }
  if (!size_client(preview.hwnd, preview.width, preview.height)) {
    (void)UndosAttachUiContext(preview.hwnd, nullptr);
    (void)DestroyWindow(preview.hwnd);
    preview_recover();
    (void)UndosCloseUi(&preview.ui);
    return 1;
  }
  if (!UndosSetFocus(preview.hwnd)) {
    ui_ok("Focus font preview");
    (void)UndosAttachUiContext(preview.hwnd, nullptr);
    (void)DestroyWindow(preview.hwnd);
    preview_recover();
    (void)UndosCloseUi(&preview.ui);
    return 1;
  }
  MSG message{};
  while (preview.running) {
    const int received = GetMessage(&message, nullptr, 0, 0);
    if (received < 0) {
      ui_ok("GetMessage");
      break;
    }
    if (!received) break;
    const uint32_t serial = preview.ui.resultSerial;
    (void)DispatchMessage(&message);
    if (preview.ui.resultSerial != serial && preview.ui.result < 0) {
      crt_printf("fontloader: UI failure %u/%u\n",
          preview.ui.operationError, preview.ui.operationStatus);
      preview.failed = true;
    }
    preview_recover();
  }
  bool destroyed = false;
  if (finish_preview_paint()) {
    if (!UndosAttachUiContext(preview.hwnd, nullptr)) {
      ui_ok("Detach font preview context");
    } else {
      destroyed = DestroyWindow(preview.hwnd);
    }
  }
  preview_recover();
  if (!UndosCloseUi(&preview.ui)) {
    crt_printf("fontloader: UI context cleanup %u/%u\n",
        preview.ui.operationError, preview.ui.operationStatus);
    destroyed = false;
  }
  if (preview.failed || !preview.painted || !destroyed) return 1;
  lifecycle("fontload: preview closed\n");
  return 0;
}

bool verify_published_font(uint32_t ppem, uint32_t generation) {
  UNDOS_FONT font = 0;
  uint32_t acquiredGeneration = 0;
  if (!UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT,
      UNDOS_GLYPH_FALLBACK_NOTDEF, &font, &acquiredGeneration))
    return ui_ok("AcquireFont");
  UNDOS_TEXT_METRICS metrics{};
  UNDOS_TEXT_EXTENT extent{};
  bool valid = acquiredGeneration == generation &&
      UndosGetFontMetrics(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_BASIC,
                          &metrics) && metrics.generation == generation &&
      UndosMeasureText(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
          UNDOS_TEXT_BASIC, "Hello", &extent) &&
      extent.advanceWidth && extent.lineHeight == metrics.lineHeight;
  const DWORD operationError = valid ? 0 : GetLastError();
  const DWORD operationStatus = valid ? 0 : UndosGetLastStatus();
  const bool closed = UndosCloseFont(font);
  if (!valid) {
    if (operationStatus) {
      crt_printf("fontloader: published font query failed %u/%u\n",
                 operationError, operationStatus);
      lifecycle("fontloader: default font query mismatch\n");
    } else {
      lifecycle("fontloader: measure text check failed\n");
    }
    return false;
  }
  if (!closed) return ui_ok("CloseFont");
  crt_printf("fontload: measured Hello width %u height %u\n",
             extent.advanceWidth, extent.lineHeight);
  lifecycle("fontload: measured Hello\n");
  crt_printf("fontload: default font set ppem %u generation %u height %u\n",
             ppem, generation, metrics.lineHeight);
  lifecycle("fontload: default font set\n");
  return true;
}
}

#if !defined(UNDOS_FONTLOADER_TEST_CALLBACKS_ONLY)
extern "C" int main(int argc, char **argv) {
  lifecycle("fontloader: start\n");
  const bool packagedPreview = argc == 2 && !strcmp(argv[1], "--preview");
  uint32_t shorthandPpem = 0;
  const bool packagedSize = argc == 2 && parse_ppem(argv[1], shorthandPpem);
  const bool packagedDefault = argc == 3 && !strcmp(argv[1], "--set-default");
  const bool previewRequested = packagedPreview || (argc == 5 && !strcmp(argv[4], "--preview"));
  const bool packagedFonts = packagedPreview || packagedDefault;
  const bool usePackagedFonts = packagedFonts || packagedSize;
  const bool setDefault = argc == 4 || previewRequested || packagedDefault || packagedSize;
  const char *regularPath = usePackagedFonts ? "/System/Fonts/SSR.TTF" : (argc > 1 ? argv[1] : nullptr);
  const char *boldPath = usePackagedFonts ? "/System/Fonts/SSB.TTF" : (setDefault ? argv[2] : nullptr);
  if ((argc != 2 && !setDefault) || !regularPath || regularPath[0] != '/' ||
      (setDefault && (!boldPath || boldPath[0] != '/'))) {
    lifecycle("Usage: /System/fontload.elf /absolute/font.ttf\n"
              "       /System/fontload.elf /absolute/regular.ttf /absolute/bold.ttf ppem [--preview]\n"
              "       /System/fontload.elf --set-default ppem\n"
              "       /System/fontload.elf ppem\n"
              "       /System/fontload.elf --preview\n");
    return 1;
  }
  uint32_t ppem = packagedPreview ? 12 : 0;
  if (setDefault && !packagedPreview) {
    if (packagedSize) {
      ppem = shorthandPpem;
    } else {
      const char *ppemArgument = packagedDefault ? argv[2] : argv[3];
      if (!parse_ppem(ppemArgument, ppem)) {
        lifecycle("fontloader: invalid ppem\n");
        return 1;
      }
    }
    if (!ppem) {
      lifecycle("fontloader: invalid ppem\n");
      return 1;
    }
  }
  uint32_t face = 0, bold = 0;
  const auto loaded = setDefault
      ? fontloader::load_pair(regularPath, boldPath, face, bold,
          [&](const char *path, uint32_t &result) {
            const auto loadedFace = load_face(path, result);
            if (loadedFace == fontloader::LoadResult::Success)
              lifecycle(path == regularPath ? "fontload: regular face registered\n"
                                            : "fontload: bold face registered\n");
            return loadedFace;
          }, close_face)
      : load_face(regularPath, face);
  if (loaded != fontloader::LoadResult::Success) return static_cast<int>(loaded);
  if (!setDefault) {
    lifecycle("fontload: registered (rendering unavailable)\n");
    if (!close_face(face)) return 1;
    lifecycle("fontload: private face closed\n");
    return 0;
  }
  uint32_t generation = 0;
  const auto status = UD_SYS_SetSystemFontV1(FontBackend_BackendTrueType, face, bold,
                                              ppem, &generation);
  // The kernel keeps the default's faces after both handles close.
  const bool closed = close_face(face) & close_face(bold);
  if (status != SyscallReturnCode_OKAY) {
    crt_printf("fontloader: set default font failed, status %u\n", status);
    lifecycle("fontloader: set default font failed\n");
    return 1;
  }
  if (!verify_published_font(ppem, generation)) return 1;
  if (!closed) return 1;
  return previewRequested ? show_preview() : 0;
}
#endif
