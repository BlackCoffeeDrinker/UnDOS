
#pragma once

#include <stdint.h>

namespace hal::x86 {
// Minimal 32-bit Task State Segment. Only esp0/ss0 (the ring0 stack used on a
// privilege-level transition into the kernel) are actively maintained; the
// rest of the fields are left zeroed since this kernel never uses hardware
// task-switching.
struct [[gnu::packed]] tss_entry_t {
  uint32_t prev_tss;
  uint32_t esp0;
  uint32_t ss0;
  uint32_t esp1;
  uint32_t ss1;
  uint32_t esp2;
  uint32_t ss2;
  uint32_t cr3;
  uint32_t eip;
  uint32_t eflags;
  uint32_t eax, ecx, edx, ebx;
  uint32_t esp, ebp, esi, edi;
  uint32_t es, cs, ss, ds, fs, gs;
  uint32_t ldt;
  uint16_t trap;
  uint16_t iomap_base;
};

static_assert(sizeof(tss_entry_t) == 104);

// GDT selector offsets (see gdt.cpp); duplicated here as plain constants so
// other translation units (e.g. context.cpp) can build ring-3 segment
// selectors (selector | 3) without depending on the templated GDT<N> type.
constexpr uint16_t GDT_SELECTOR_KERNEL_CODE = 0x08;
constexpr uint16_t GDT_SELECTOR_KERNEL_DATA = 0x10;
constexpr uint16_t GDT_SELECTOR_USER_DATA = 0x18;
constexpr uint16_t GDT_SELECTOR_USER_CODE = 0x20;
constexpr uint16_t GDT_SELECTOR_TSS = 0x28;

// Updates esp0 so a ring-3 -> ring-0 transition lands on the given kernel stack.
// The TSS descriptor itself is installed as part of init_gdt() (see gdt.cpp),
// since it lives in the same GDT.
void tss_set_esp0(uint32_t esp0) noexcept;
}// namespace hal::x86
