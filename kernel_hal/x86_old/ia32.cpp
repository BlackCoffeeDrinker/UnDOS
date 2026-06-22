
#include "ia32.hpp"
#include "gdt.hpp"
#include "intr.hpp"
#include "pmm.hpp"
#include "structs.hpp"

#include "strfmt.hpp"

/* Check if the BIT in FLAGS is set. */
#define CHECK_FLAG(flags, bit) ((flags) & (1 << (bit)))

// Static storage so it persists when we cross over to src/
static kernel::memory_region_t internal_mmap[64];

hal::x86::regs cpu_cpuid(int code) {
  hal::x86::regs r{};
  asm volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx),
                         "=c"(r.ecx), "=d"(r.edx) : "0"(code));
  return r;
}

UNDOS_HAL_API void HAL_PlatformInit(const kernel::boot_info_t &boot_info) {
  early_print_fmt("HAL\r\n");
  hal::x86::init_gdt();
  hal::x86::init_idt();
  hal::x86::init_pmm(boot_info);
  early_print("After init_pmm\n\r");
  
}

namespace std {
[[noreturn]] void terminate() noexcept {
  while (true);
}

}// namespace std
