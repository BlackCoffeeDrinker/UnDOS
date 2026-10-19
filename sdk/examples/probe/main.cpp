#include <undoscrt/heap.h>
#include <undoscrt/process.h>
#include <undoscrt/string.h>
#include <userspace.h>
#include <undos/font/resource.hpp>

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
