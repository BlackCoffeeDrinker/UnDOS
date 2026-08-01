#include "../userspace.h"
#include <undos/syscalls.h>

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
  
  
  UD_SYS_PrintV1("USER SPACE: NativeElfLoader (C++): exec_path = ");
  UD_SYS_PrintV1(exec_path);
  UD_SYS_PrintV1("\n");

  uint32_t fd;
  if (UD_SYS_OpenV1(exec_path, FileOpenMode_Read, &fd) != SyscallReturnCode_OKAY) {
    (void) UD_SYS_ExitV1(255);
    __builtin_unreachable();
  }

  // read ELF header
  {
    char e_ident[16];
    size_t size = sizeof(e_ident);
    if (UD_SYS_ReadV1(fd, reinterpret_cast<uint8_t *>(e_ident), &size) != SyscallReturnCode_OKAY) {
      (void) UD_SYS_CloseV1(fd);
      (void) UD_SYS_ExitV1(255);
      __builtin_unreachable();
    }

    // Make sure it's an ELF file
    if (e_ident[0] != 0x7F || e_ident[1] != 'E' || e_ident[2] != 'L' || e_ident[3] != 'F') {
      (void) UD_SYS_CloseV1(fd);
      (void) UD_SYS_ExitV1(255);
      __builtin_unreachable();
    }

    UD_SYS_PrintV1("USER SPACE: NativeElfLoader is valid\r\n");
  }

  // read ELF, mmap segments, jump to entry


  // Later: open ELF, read ELF, mmap segments, jump to entry
  (void) UD_SYS_ExitV1(0);
  __builtin_unreachable();
}
