#ifdef __INT8_TYPE__
typedef __INT8_TYPE__ int8_t;
#endif
#ifdef __INT16_TYPE__
typedef __INT16_TYPE__ int16_t;
#endif
#ifdef __INT32_TYPE__
typedef __INT32_TYPE__ int32_t;
#endif
#ifdef __INT64_TYPE__
typedef __INT64_TYPE__ int64_t;
#endif
#ifdef __UINT8_TYPE__
typedef __UINT8_TYPE__ uint8_t;
#endif
#ifdef __UINT16_TYPE__
typedef __UINT16_TYPE__ uint16_t;
#endif
#ifdef __UINT32_TYPE__
typedef __UINT32_TYPE__ uint32_t;
#endif
#ifdef __UINT64_TYPE__
typedef __UINT64_TYPE__ uint64_t;
#endif


static inline uint32_t syscall3(uint32_t num, uint32_t a, uint32_t b, uint32_t c) {
  uint32_t ret;
  asm volatile(
      "int $0x80"
      : "=a"(ret)
      : "a"(num), "b"(a), "c"(b), "d"(c)
      : "memory");
  return ret;
}

static void sys_write(const char *s) {
  uint32_t len = 0;
  while (s[len] != '\0') len++;
  syscall3(1, reinterpret_cast<uint32_t>(s), len, 0);
}

static void sys_exit(int code) {
  syscall3(0, static_cast<uint32_t>(code), 0, 0);
}

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
  sys_exit(0);
  __builtin_unreachable();
}
