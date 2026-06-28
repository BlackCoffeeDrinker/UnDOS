
#include "ia32.hpp"
#include "gdt.hpp"
#include "intr.hpp"
#include "pmm.hpp"

hal::x86::regs cpu_cpuid(int code) {
  hal::x86::regs r{};
  asm volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx),
                         "=c"(r.ecx), "=d"(r.edx) : "0"(code));
  return r;
}

UNDOS_HAL_API void HAL_Platform_Init(const kernel::BootInfoT &boot_info) noexcept {
  hal::x86::init_gdt();
  hal::x86::init_idt();
  hal::x86::init_pmm(boot_info);
}

UNDOS_HAL_API void HAL_CPU_Halt() noexcept {
  __asm__ volatile("hlt");
}

UNDOS_HAL_API [[noreturn]] void HAL_Platform_Panic(const char *message, const char *file, int line) noexcept {
  early_print_fmt("\r\n--------\r\nPANIC: {} at {}:{}\r\nSystem Halted\r\n", message, file, line);

  while (true) {
    __asm__ volatile("cli; hlt");
  }
}

UNDOS_HAL_API void HAL_Platform_InitializeSystemTimer() noexcept {
}

namespace std {
[[noreturn]] void terminate() noexcept {
  HAL_Platform_Panic("std::terminate() called", __FILE__, __LINE__);
}
}// namespace std
