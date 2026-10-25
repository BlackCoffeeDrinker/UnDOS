#include <undoscrt/heap.h>
#include <undoscrt/process.h>
#include <undoscrt/string.h>
#include <userspace.h>
#include <undos/font/resource.hpp>
#include <undosgui/gui.hpp>

static_assert(sizeof(HANDLE) == 4 && alignof(HANDLE) == 4);
static_assert(sizeof(__SYS_PROC_CREATEPROCESS_V2) == 36 &&
              offsetof(__SYS_PROC_CREATEPROCESS_V2, stdinHandle) == 20 &&
              offsetof(__SYS_PROC_CREATEPROCESS_V2, stdoutHandle) == 24 &&
              offsetof(__SYS_PROC_CREATEPROCESS_V2, stderrHandle) == 28 &&
              offsetof(__SYS_PROC_CREATEPROCESS_V2, handle) == 32);

extern "C" int sdk_value();
extern "C" int sdk_query_from_c();
extern "C" int sdk_libc_from_c();

namespace {
unsigned phase;
[[noreturn]] void fail() {
  (void) UD_SYS_PrintV1("SDK probe: FAIL");
  _Exit(1);
}

void check_font_layout() {
  gui::TextExtent narrow{}, wide{}, measured{}, fitted{};
  const gui::Font font = gui::Font::acquire();
  if (!font) fail();
  const gui::TextLayout layout(font);
  if (!layout.measure("iiii", narrow) || !layout.measure("WWWW", wide) ||
      narrow.width >= wide.width) fail();
  char output[64]{};
  if (!layout.fit_prefix("WWWW", output, sizeof(output), wide.width, fitted) ||
      strcmp(output, "WWWW") || fitted.width != wide.width) fail();
  if (!layout.fit_prefix("WWWW", output, sizeof(output), 0, fitted) || output[0] ||
      fitted.width || fitted.height != wide.height) fail();
  if (!layout.fit_prefix("", output, sizeof(output), 100, fitted) || output[0] || fitted.width) fail();
  if (!layout.measure("...", measured) ||
      !layout.fit_prefix("WWWWWWWW", output, 4, measured.width, fitted) || strcmp(output, "...")) fail();
  if (!layout.measure("\xc3\xa9...", measured) ||
      !layout.fit_prefix("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", output,
                     sizeof(output), measured.width, fitted) ||
      fitted.width > measured.width || strcmp(output, "\xc3\xa9...")) fail();
  fitted = {99, 99};
  if (layout.fit_prefix("x", output, 3, 100, fitted) || fitted.width || fitted.height) fail();
  char oversized[1026];
  memset(oversized, 'W', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = 0;
  if (layout.fit_prefix(oversized, output, sizeof(output), 100, fitted)) fail();
  (void)UD_SYS_PrintV1("SDK probe: proportional text fitting PASS");
}
}

extern "C" void sdk_record(unsigned value) {
  if (phase + 1 != value) fail();
  phase = value;
  if (value == 7) (void) UD_SYS_PrintV1("SDK probe: shared-library cleanup PASS");
}

namespace {
struct Lifetime {
  Lifetime() { sdk_record(2); }
  ~Lifetime() { sdk_record(4); }
} lifetime;
void callback() { sdk_record(3); }
__attribute__((destructor)) void finalize() { sdk_record(6); }
}

int main() {
  if (phase != 2 || sdk_value() != 73 || !environ || environ[0]) fail();
  if (!sdk_query_from_c()) fail();
  if (!sdk_libc_from_c()) fail();
  check_font_layout();
  if (undos::font::validate({nullptr, 0}) != undos::font::Error::InvalidSize) fail();
  auto *memory = static_cast<char *>(crt_malloc(64));
  if (!memory) fail();
  memset(memory, 0, 64);
  memcpy(memory, "SDK", 4);
  if (strcmp(memory, "SDK")) fail();
  crt_free(memory);
  if (atexit(callback)) fail();
  (void) UD_SYS_PrintV1("SDK probe: installed runtime PASS");
  return 0;
}
