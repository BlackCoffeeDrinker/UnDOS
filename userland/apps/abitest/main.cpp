#include <undoscrt/process.h>
#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <userspace.h>
#include <undos/syscalls.h>

extern "C" int abi_shared_value();
extern "C" int abi_dependent_value();

static_assert(sizeof(int) == 4 && sizeof(long) == 4 && sizeof(void *) == 4);
static_assert(sizeof(long long) == 8 && sizeof(long double) == 12);

namespace {
unsigned phase;
int initialized_data = 73;
unsigned zero_data[32];

[[noreturn]] void fail() {
  (void) UD_SYS_PrintV1("ABI probe: FAIL");
  crt_immediate_exit(255);
}

struct Lifetime {
  Lifetime();
  ~Lifetime();
} lifetime;
}

extern "C" __attribute__((visibility("default"))) void abi_record(unsigned value) {
  if (phase + 1 != value) fail();
  phase = value;
  if (value == 8) (void) UD_SYS_PrintV1("ABI probe: lifecycle PASS");
}

namespace {
Lifetime::Lifetime() { abi_record(3); }
Lifetime::~Lifetime() { abi_record(5); }
void callback() { abi_record(4); }
__attribute__((destructor)) void finalize() { abi_record(6); }
}

extern "C" int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--immediate")) {
    if (atexit(fail)) fail();
    (void) UD_SYS_PrintV1("ABI probe: immediate exit PASS");
    _Exit(static_cast<int>(0x87654321u));
  }
  if (phase != 3 || initialized_data != 73 || abi_shared_value() != 73 || abi_dependent_value() != 73 ||
      argc != 4 || !argv || argv[argc] || strcmp(argv[0], "/System/abitest.elf") ||
      strcmp(argv[1], "two words") || strcmp(argv[2], "") || strcmp(argv[3], "last") ||
      !environ || environ[0]) fail();
  for (auto value : zero_data) if (value) fail();
  FpuCapability capability{};
  if (UD_SYS_QueryFpuV1(&capability) != SyscallReturnCode_OKAY ||
      capability != FpuCapability_Unavailable) fail();
  HANDLE handle = 0;
  if (UD_SYS_GetStdHandleV1(static_cast<StdHandleSelector>(99), &handle) !=
      SyscallReturnCode_INVALID_ARGUMENT) fail();
  for (unsigned i = 0; i < 3; ++i) {
    const auto selector = static_cast<StdHandleSelector>(i);
    HANDLE cached = 0;
    const auto result = UD_SYS_GetStdHandleV1(selector, &handle);
    const int cache_result = crt_get_std_handle(selector, &cached);
    if (result == SyscallReturnCode_OKAY) {
      if (cache_result || cached != handle) fail();
    } else if (result != SyscallReturnCode_INVALID_ARGUMENT || cache_result != -1) fail();
    else if (selector == StdHandleSelector_Stdout && crt_puts("unavailable") != -1) fail();
  }
  if (atexit(callback)) fail();
  (void) UD_SYS_PrintV1("ABI probe: arguments/data/environment/handles/FPU PASS");
  return 0x12345678;
}
