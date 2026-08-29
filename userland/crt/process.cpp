#include <undoscrt/process.h>

#include "../userspace.h"
#include <undos/syscalls.h>

extern "C" [[noreturn]] void crt_exit(int exitCode) {
  (void) UD_SYS_ExitV1(static_cast<uint32_t>(exitCode));
  __builtin_unreachable();
}
