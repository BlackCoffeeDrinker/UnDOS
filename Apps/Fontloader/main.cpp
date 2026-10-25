#include "decoder.hpp"
#include <undoscrt/heap.h>
#include <undoscrt/process.h>
#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <undosgui/gui.hpp>
#include <userspace.h>

namespace {
void lifecycle(const char *message) {
  crt_puts(message);
  (void)UD_SYS_PrintV1(message);
}

[[noreturn]] void allocation_failure() {
  lifecycle("fontloader: allocation failed\n");
  crt_exit(1);
}
}

// The CRT exposes malloc/free, while the reusable decoder uses kstd containers.
void *operator new(size_t size) {
  void *result = crt_malloc(size ? size : 1);
  if (!result) allocation_failure();
  return result;
}
void *operator new[](size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept { crt_free(p); }
void operator delete[](void *p) noexcept { crt_free(p); }
void operator delete(void *p, size_t) noexcept { crt_free(p); }
void operator delete[](void *p, size_t) noexcept { crt_free(p); }

namespace {
bool load_face(const char *path, uint32_t &face) {
  using namespace undos::font;
  HANDLE fileHandle = 0;
  const auto opened = UD_SYS_OpenV1(path, FileOpenMode_Read, &fileHandle);
  if (opened != SyscallReturnCode_OKAY) {
    crt_printf("fontloader: open failed, status %u\n", opened);
    lifecycle("fontloader: file open failed\n");
    return false;
  }
  kstd::vector<uint8_t> file;
  uint8_t chunk[4096];
  while (true) {
    uint32_t count = sizeof(chunk);
    const auto status = UD_SYS_ReadV1(fileHandle, chunk, &count);
    if (status != SyscallReturnCode_OKAY || count > sizeof(chunk) ||
        count > MaxFileBytes - file.size()) {
      (void)UD_SYS_CloseV1(fileHandle);
      crt_printf("fontloader: read failed or file exceeds 4 MiB, status %u\n", status);
      lifecycle("fontloader: read/size rejected\n");
      return false;
    }
    if (!count) break;
    for (uint32_t i = 0; i < count; ++i) file.push_back(chunk[i]);
  }
  (void)UD_SYS_CloseV1(fileHandle);
  lifecycle("fontloader: file read\n");
  kstd::vector<uint8_t> resource;
  const auto decoded = loader::decode({.data = file.data(), .size = file.size()}, resource);
  if (decoded != loader::DecodeError::None) {
    crt_printf("fontloader: decode failed: %s\n", loader::error_message(decoded));
    (void)UD_SYS_PrintV1(loader::error_message(decoded));
    lifecycle("fontloader: decode rejected\n");
    return false;
  }
  const auto validation = validate({.data = resource.data(), .size = resource.size()});
  if (validation != Error::None) {
    crt_printf("fontloader: normalized validation failed: %s\n", error_message(validation));
    lifecycle("fontloader: normalized resource rejected\n");
    return false;
  }
  lifecycle("fontloader: decoded and structurally validated (no execution)\n");
  uint32_t upload = 0;
  auto status = UD_SYS_BeginFontUploadV1(uint32_t(resource.size()), &upload);
  if (status != SyscallReturnCode_OKAY) {
    crt_printf("fontloader: begin upload failed, status %u\n", status);
    lifecycle("fontloader: begin upload failed\n");
    return false;
  }
  lifecycle("fontloader: upload begun\n");
  for (uint32_t offset = 0; offset < resource.size();) {
    const uint32_t remaining = uint32_t(resource.size()) - offset;
    const uint32_t size = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
    status = UD_SYS_AppendFontUploadV1(upload, offset, resource.data() + offset, size);
    if (status != SyscallReturnCode_OKAY) {
      (void)UD_SYS_CloseFontResourceV1(upload);
      crt_printf("fontloader: append at %u failed, status %u\n", offset, status);
      lifecycle("fontloader: upload aborted\n");
      return false;
    }
    offset += size;
  }
  uint32_t validationError = 0;
  status = UD_SYS_CommitFontUploadV1(upload, &face, &validationError);
  if (status != SyscallReturnCode_OKAY) {
    // Failed structural commits may already have destroyed the staging handle.
    (void)UD_SYS_CloseFontResourceV1(upload);
    crt_printf("fontloader: commit failed, status %u, validation %u\n", status, validationError);
    lifecycle("fontloader: commit rejected\n");
    return false;
  }
  return true;
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
  if (status == SyscallReturnCode_OKAY) return true;
  crt_printf("fontloader: face close failed, status %u\n", status);
  lifecycle("fontloader: face close failed\n");
  return false;
}

struct Preview {
  uint32_t hwnd{0};
  bool running{true};
  bool painted{false};
  bool failed{false};
  gui::TextExtent regular;
  gui::TextExtent bold;
  gui::TextExtent transparent;
  int regularY{0};
  int boldY{0};
  int transparentY{0};
  int width{420};
  int height{0};
};
Preview preview;
constexpr const char *PreviewText = "Hello, UnDOS! Wiim 012345";

// Sizes the frame so its client area, not its outer rectangle, holds the content.
bool size_client(uint32_t hwnd, int width, int height) {
  Rect outer{};
  if (!gui::check(UD_SYS_GetWindowRectV1(hwnd, &outer), "Get font preview rectangle")) return false;
  const Rect inner = gui::client(hwnd);
  const int64_t frameWidth = static_cast<int64_t>(outer.right) - outer.left - inner.right;
  const int64_t frameHeight = static_cast<int64_t>(outer.bottom) - outer.top - inner.bottom;
  const int64_t outerWidth = width + frameWidth;
  const int64_t outerHeight = height + frameHeight;
  if (frameWidth < 0 || frameHeight < 0 || outerWidth > INT32_MAX || outerHeight > INT32_MAX) return false;
  return gui::move(hwnd, outer.left, outer.top, static_cast<int>(outerWidth), static_cast<int>(outerHeight));
}

bool layout_preview() {
  gui::Font &font = gui::system_font();
  gui::TextMetrics metrics{};
  if (!font.metrics(metrics) || !metrics.lineHeight ||
      !font.measure(PreviewText, preview.regular) ||
      !font.measure(PreviewText, preview.bold, gui::FontStyle::Bold) ||
      !font.measure("Transparent text", preview.transparent) ||
      metrics.generation != font.generation() ||
      preview.regular.generation != font.generation() ||
      preview.bold.generation != font.generation() ||
      preview.transparent.generation != font.generation()) return false;
  constexpr int gap = 8;
  constexpr int padding = 12;
  const uint64_t boldY = static_cast<uint64_t>(preview.regular.height) + gap + padding;
  const uint64_t transparentY = boldY + preview.bold.height + gap;
  const uint32_t transparentHeight =
      preview.transparent.height > metrics.lineHeight ? preview.transparent.height : metrics.lineHeight;
  const uint64_t contentHeight = transparentY + transparentHeight + padding;
  uint32_t textWidth = preview.regular.width;
  if (preview.bold.width > textWidth) textWidth = preview.bold.width;
  if (preview.transparent.width > textWidth) textWidth = preview.transparent.width;
  const uint64_t contentWidth = static_cast<uint64_t>(textWidth) + 2 * padding;
  if (boldY > INT32_MAX || transparentY > INT32_MAX ||
      contentHeight > INT32_MAX || contentWidth > INT32_MAX) return false;
  preview.regularY = padding;
  preview.boldY = static_cast<int>(boldY);
  preview.transparentY = static_cast<int>(transparentY);
  preview.height = static_cast<int>(contentHeight);
  preview.width = static_cast<int>(contentWidth > 420 ? contentWidth : 420);
  return !preview.hwnd || size_client(preview.hwnd, preview.width, preview.height);
}

void preview_proc(const Msg &message) {
  if (message.hwnd != preview.hwnd) return;
  if (message.message == WindowMessage_Close ||
      (message.message == WindowMessage_KeyDown && message.wParam == 27)) {
    preview.running = false;
  } else if (message.message == WindowMessage_FontChanged) {
    if (!layout_preview()) {
      preview.failed = true;
      preview.running = false;
      return;
    }
    gui::invalidate(message.hwnd);
  } else if (message.message == WindowMessage_Paint) {
    gui::Paint paint(message.hwnd);
    gui::TextMetrics metrics{};
    if (!gui::system_font().metrics(metrics)) {
      preview.failed = true;
      preview.running = false;
      return;
    }
    const bool okay = paint &&
        paint.draw_text(12, preview.regularY, PreviewText, gui::FontStyle::Regular, gui::Ink, gui::Paper) &&
        paint.draw_text(12, preview.boldY, PreviewText, gui::FontStyle::Bold, gui::Ink, gui::Paper);
    if (!okay) {
      lifecycle("fontload: preview FAIL\n");
      preview.failed = true;
      preview.running = false;
      return;
    }
    paint.fill(12, preview.transparentY,
               12 + static_cast<int>(preview.transparent.width),
               preview.transparentY + static_cast<int>(preview.transparent.height), gui::Face);
    if (!paint.draw_text(12, preview.transparentY, "Transparent text", gui::FontStyle::Regular,
                         gui::Highlight, gui::Paper, false)) {
      lifecycle("fontload: preview FAIL\n");
      preview.failed = true;
      preview.running = false;
      return;
    }
    paint.fill(12, preview.regularY + static_cast<int>(preview.regular.height),
               12 + static_cast<int>(preview.regular.width),
               preview.regularY + 1 + static_cast<int>(preview.regular.height),
               gui::Highlight);
    paint.fill(12, preview.boldY + static_cast<int>(preview.bold.height),
               12 + static_cast<int>(preview.bold.width), preview.boldY + 1 + static_cast<int>(preview.bold.height),
               gui::Highlight);
    if (!preview.painted) {
      preview.painted = true;
      lifecycle("fontload: preview painted\n");
    }
  }
}

int show_preview() {
  if (!layout_preview() || !gui::register_class("FontPreview", preview_proc, gui::Paper))
    return 1;
  preview.hwnd = gui::window("FontPreview", "Default font: regular and bold", 160, 120,
                             preview.width, preview.height);
  if (!preview.hwnd) return 1;
  if (!size_client(preview.hwnd, preview.width, preview.height)) {
    (void)gui::destroy(preview.hwnd);
    return 1;
  }
  if (!gui::check(UD_SYS_SetFocusV1(preview.hwnd), "Focus font preview")) {
    (void)gui::destroy(preview.hwnd);
    return 1;
  }
  Msg message{};
  while (preview.running && gui::next(message)) gui::dispatch(message);
  const bool destroyed = gui::destroy(preview.hwnd);
  if (preview.failed || !preview.painted || !destroyed) return 1;
  lifecycle("fontload: preview closed\n");
  return 0;
}
}

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
  uint32_t face = 0;
  if (!load_face(regularPath, face)) return 1;
  if (!setDefault) {
    lifecycle("fontload: registered (rendering unavailable)\n");
    if (!close_face(face)) return 1;
    lifecycle("fontload: private face closed\n");
    return 0;
  }
  lifecycle("fontload: regular face registered\n");
  uint32_t bold = 0;
  if (!load_face(boldPath, bold)) {
    (void)close_face(face);
    return 1;
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
  gui::Font font = gui::Font::acquire();
  gui::TextMetrics metrics{};
  if (!font || font.generation() != generation || !font.metrics(metrics) ||
      metrics.generation != generation) {
    lifecycle("fontloader: default font query mismatch\n");
    return 1;
  }
  gui::TextExtent extent{};
  if (!font.measure("Hello", extent) || !extent.width ||
      extent.height != metrics.lineHeight) {
    lifecycle("fontloader: measure text check failed\n");
    return 1;
  }
  crt_printf("fontload: measured Hello width %u height %u\n", extent.width, extent.height);
  lifecycle("fontload: measured Hello\n");
  crt_printf("fontload: default font set ppem %u generation %u height %u\n",
             ppem, generation, metrics.lineHeight);
  lifecycle("fontload: default font set\n");
  if (!closed) return 1;
  return previewRequested ? show_preview() : 0;
}
