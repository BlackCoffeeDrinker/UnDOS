#include <undoscrt/heap.h>
#include <undoscrt/process.h>
#include <undoscrt/string.h>
#include <userspace.h>
#include <undos/font/resource.hpp>
#include <windows.h>
#include <undosgui/extensions.h>

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
  UNDOS_TEXT_EXTENT narrow{}, wide{}, measured{}, fitted{};
  UNDOS_FONT font = 0;
  uint32_t generation = 0;
  if (!UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT,
      UNDOS_GLYPH_FALLBACK_NOTDEF, &font, &generation))
    fail();
  (void)generation;
  if (!UndosMeasureText(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "iiii", &narrow) ||
      !UndosMeasureText(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "WWWW", &wide) ||
      narrow.advanceWidth >= wide.advanceWidth)
    fail();
  char output[64]{};
  if (!UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "WWWW", output, sizeof(output), wide.advanceWidth,
      &fitted) || strcmp(output, "WWWW") ||
      fitted.advanceWidth != wide.advanceWidth)
    fail();
  if (!UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "WWWW", output, sizeof(output), 0, &fitted) ||
      output[0] || fitted.advanceWidth || fitted.lineHeight != wide.lineHeight)
    fail();
  if (!UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "", output, sizeof(output), 100, &fitted) ||
      output[0] || fitted.advanceWidth)
    fail();
  if (!UndosMeasureText(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "...", &measured) ||
      !UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "WWWWWWWW", output, 4, measured.advanceWidth,
      &fitted) || strcmp(output, "..."))
    fail();
  if (!UndosMeasureText(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "\xc3\xa9...", &measured) ||
      !UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9",
      output, sizeof(output), measured.advanceWidth, &fitted) ||
      fitted.advanceWidth > measured.advanceWidth ||
      strcmp(output, "\xc3\xa9..."))
    fail();
  if (!UndosFitTextTail(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "WWWWWWWW", output, sizeof(output),
      wide.advanceWidth, &fitted) || fitted.advanceWidth > wide.advanceWidth)
    fail();
  memset(output, 'x', sizeof(output));
  fitted = {99, 98, 97, 96, 95, 94, 93, 92};
  if (UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, "x", output, 3, 100, &fitted) ||
      output[0] != 'x' || fitted.advanceWidth != 98 ||
      fitted.lineHeight != 97)
    fail();
  char oversized[1026];
  memset(oversized, 'W', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = 0;
  if (UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
      UNDOS_TEXT_BASIC, oversized, output, sizeof(output), 100, &fitted))
    fail();
  if (!UndosCloseFont(font)) fail();
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
