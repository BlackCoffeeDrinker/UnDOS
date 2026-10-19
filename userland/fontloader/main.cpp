#include "decoder.hpp"
#include <undoscrt/heap.h>
#include <undoscrt/process.h>
#include <undoscrt/stdio.h>
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

extern "C" int main(int argc, char **argv) {
  using namespace undos::font;
  lifecycle("fontloader: start (structural registration only)\n");
  if (argc != 2 || !argv[1] || argv[1][0] != '/') {
    lifecycle("Usage: /System/fontload.elf /absolute/font.ttf\n");
    return 1;
  }
  HANDLE fileHandle = 0;
  const auto opened = UD_SYS_OpenV1(argv[1], FileOpenMode_Read, &fileHandle);
  if (opened != SyscallReturnCode_OKAY) {
    crt_printf("fontloader: open failed, status %u\n", opened);
    lifecycle("fontloader: file open failed\n");
    return 1;
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
      return 1;
    }
    if (!count) break;
    for (uint32_t i = 0; i < count; ++i) file.push_back(chunk[i]);
  }
  (void)UD_SYS_CloseV1(fileHandle);
  lifecycle("fontloader: file read\n");
  kstd::vector<uint8_t> resource;
  const auto decoded = loader::decode({file.data(), file.size()}, resource);
  if (decoded != loader::DecodeError::None) {
    crt_printf("fontloader: decode failed: %s\n", loader::error_message(decoded));
    (void)UD_SYS_PrintV1(loader::error_message(decoded));
    lifecycle("fontloader: decode rejected\n");
    return 1;
  }
  const auto validation = validate({resource.data(), resource.size()});
  if (validation != Error::None) {
    crt_printf("fontloader: normalized validation failed: %s\n", error_message(validation));
    lifecycle("fontloader: normalized resource rejected\n");
    return 1;
  }
  lifecycle("fontloader: decoded and structurally validated (no execution)\n");
  uint32_t upload = 0;
  auto status = UD_SYS_BeginFontUploadV1(uint32_t(resource.size()), &upload);
  if (status != SyscallReturnCode_OKAY) {
    crt_printf("fontloader: begin upload failed, status %u\n", status);
    lifecycle("fontloader: begin upload failed\n");
    return 1;
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
      return 1;
    }
    offset += size;
  }
  uint32_t face = 0, validationError = 0;
  status = UD_SYS_CommitFontUploadV1(upload, &face, &validationError);
  if (status != SyscallReturnCode_OKAY) {
    // Failed structural commits may already have destroyed the staging handle.
    (void)UD_SYS_CloseFontResourceV1(upload);
    crt_printf("fontloader: commit failed, status %u, validation %u\n", status, validationError);
    lifecycle("fontloader: commit rejected\n");
    return 1;
  }
  lifecycle("fontload: registered (rendering unavailable)\n");
  status = UD_SYS_CloseFontResourceV1(face);
  if (status != SyscallReturnCode_OKAY) {
    crt_printf("fontloader: face close failed, status %u\n", status);
    lifecycle("fontloader: face close failed\n");
    return 1;
  }
  lifecycle("fontload: private face closed\n");
  return 0;
}
