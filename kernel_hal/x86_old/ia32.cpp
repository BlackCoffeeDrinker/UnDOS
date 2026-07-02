
#include "ia32.hpp"
#include "gdt.hpp"
#include "intr.hpp"
#include "pmm.hpp"

#include <Kernel.hpp>

hal::x86::regs cpu_cpuid(int code) {
  hal::x86::regs r{};
  asm volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx),
                         "=c"(r.ecx), "=d"(r.edx) : "0"(code));
  return r;
}

UNDOS_HAL_API void HAL_PLATFORM_Init(const kernel::BootInfoT &boot_info) noexcept {
  hal::x86::init_gdt();
  hal::x86::init_idt();
  hal::x86::init_pmm(boot_info);
}

UNDOS_HAL_API void HAL_CPU_ReloadContext() noexcept {
  hal::x86::reload_gdt();
}

UNDOS_HAL_API uint32_t HAL_PLATFORM_GetCpuCount() noexcept {
  return 1;
}

UNDOS_HAL_API void HAL_CPU_Halt() noexcept {
  __asm__ volatile("hlt");
}

UNDOS_HAL_API void HAL_IO_Out8(uint16_t port, uint8_t val) noexcept {
  asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

UNDOS_HAL_API uint8_t HAL_IO_In8(uint16_t port) noexcept {
  uint8_t ret;
  asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

UNDOS_HAL_API void HAL_IO_Delay() noexcept {
  HAL_IO_Out8(0x80, 0);
}

UNDOS_HAL_API [[noreturn]] void HAL_PLATFORM_Panic(const char *message, const char *file, int line) noexcept {
  early_print_fmt("\r\n--------\r\nPANIC: {} at {}:{}\r\nSystem Halted\r\n", message, file, line);

  while (true) {
    __asm__ volatile("cli; hlt");
  }
}

UNDOS_HAL_API void HAL_PLATFORM_InitializeSystemTimer() noexcept {
}

UNDOS_HAL_API [[noreturn]] void HAL_PLATFORM_Shutdown() noexcept {
  while (true) {
    __asm__ volatile("cli; hlt");
  }
}

UNDOS_HAL_API void HAL_PLATFORM_AfterObjectManager() noexcept {
  KE_DRIVER_Load(R"(\System\Initial\BootModules\isa_bus)");
  // Always add ISA
  // Detect PCI
}

namespace std {
[[noreturn]] void terminate() noexcept {
  HAL_PLATFORM_Panic("std::terminate() called", __FILE__, __LINE__);
}
}// namespace std
