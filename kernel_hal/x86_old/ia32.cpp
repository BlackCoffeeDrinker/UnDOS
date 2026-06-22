
#include "ia32.hpp"
#include "boot/multiboot.hpp"
#include "debug_serial.hpp"
#include "gdt.hpp"
#include "intr.hpp"
#include "structs.hpp"

#include "strfmt.hpp"

/* Check if the BIT in FLAGS is set. */
#define CHECK_FLAG(flags, bit) ((flags) & (1 << (bit)))

// Static storage so it persists when we cross over to src/
static kernel::memory_region_t internal_mmap[64];

kernel::x86::regs cpu_cpuid(int code) {
  kernel::x86::regs r{};
  asm volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx),
                         "=c"(r.ecx), "=d"(r.edx) : "0"(code));
  return r;
}

UNDOS_HAL_API void HAL_PlatformInit(const kernel::boot_info_t &boot_info) {
  kernel::x86::early::init_serial();

  kernel::arch::early_print_fmt("HAL\r\n");
  kernel::x86::init_gdt();
  kernel::x86::init_idt();
}

namespace std {
[[noreturn]] void terminate() noexcept {
  while (true);
}

}// namespace std
