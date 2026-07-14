
#include "syscall.hpp"

#include "process.hpp"

#include <Kernel.hpp>
#include <kernel/syscall.hpp>

namespace {
// sys_write(buf, len): forwards straight to the existing debug/console
// output path so calling user-mode code is observable over the serial
// console (a single, implicit "stdout" stream for this milestone).
int32_t sys_write(uint32_t buf, uint32_t len) noexcept {
  if (buf == 0 || len == 0) {
    return 0;
  }

  const auto *data = reinterpret_cast<const char *>(static_cast<uintptr_t>(buf));
  for (uint32_t i = 0; i < len; ++i) {
    early_print_char(data[i]);
  }

  return static_cast<int32_t>(len);
}

// sys_exit(code): never returns to the caller.
[[noreturn]] void sys_exit(uint32_t code) noexcept {
  kernel::process::terminate_current(static_cast<int32_t>(code));
}
}// namespace


UNDOS_KERNEL_API_DEF int32_t KE_SYSCALL_Dispatch(uint32_t number, uint32_t arg0, uint32_t arg1, uint32_t arg2) noexcept {
  (void) arg2;
  
  //early_print_fmt("Syscall: 0x{:x}, arg0: 0x{:x}, arg1: 0x{:x}, arg2: 0x{:x}\r\n", number, arg0, arg1, arg2);

  switch (number) {
    case SYS_EXIT:
      sys_exit(arg0);
    case SYS_WRITE:
      return sys_write(arg0, arg1);
    default:
      return -1;
  }
}
