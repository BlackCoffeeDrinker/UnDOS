#include "../userspace.h"
#include <undos/syscalls.h>

int 

// The kernel hands control to `_start` with ESP pointing straight at the
// pushed exec_path pointer (see KE_USER_CreateProcess). A normal C++
// function's prologue (push ebp / sub esp, N for locals) would move ESP
// before we could read that word, so `_start` is a bare assembly trampoline
// that grabs [esp] first -- before anything else touches the stack -- and
// hands it off to loader_main as a regular cdecl argument.
extern "C" void _start();
asm(
    ".text\n\t"
    ".global _start\n\t"
    ".type _start, @function\n\t"
    "_start:\n\t"
    "    movl (%esp), %eax\n\t"
    "    pushl %eax\n\t"
    "    call loader_main\n\t"
    ".size _start, . - _start\n\t");

extern "C" [[noreturn]] void loader_main(const char *exec_path) {
  sys_write("USER SPACE: NativeElfLoader (C++): exec_path = ");
  sys_write(exec_path);
  sys_write("\n");

  // Later: open ELF, read ELF, mmap segments, jump to entry
  sys_write("USER SPACE: NativeElfLoader: ELF loading not implemented yet, exitting\n");
  UD_SYS_ExitV1(0);
  __builtin_unreachable();
}
