
#pragma once

#include <stdint.h>

#include <type_traits.hpp>
#include <utility.hpp>

namespace kernel::x86 {
// region gdt
struct [[gnu::packed]] gdtr_t {
  uint16_t limit;
  uint32_t base;

  constexpr explicit gdtr_t(uint32_t base_, uint16_t limit_) noexcept
      : limit(limit_), base(base_) {}
};

enum class Descriptor : uint8_t {
  ACCESS = 0b00000001,
  READWRITE = 0b00000010,
  DC = 0b00000100,
  EXECUTABLE = 0b00001000,
  CODE_DATA = 0b00010000,
  DPL0 = 0b00000000,
  DPL1 = 0b00100000,
  DPL2 = 0b01000000,
  DPL3 = 0b01100000,
  PRESENT = 0b10000000,
};

enum class Granularity : uint8_t {
  LIMIT_HIGH = 0x0F,// low 4 bits
  OS = 0b00010000,
  X64 = 0b00100000,
  X32 = 0b01000000,
  BIG_PAGES_4K = 0b10000000,
};

struct [[gnu::packed]] gdt_entry_t {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_mid;
  uint8_t access;
  uint8_t granularity;
  uint8_t base_high;

  constexpr gdt_entry_t() : limit_low(0), base_low(0), base_mid(0), access(0), granularity(0), base_high(0) {}

  constexpr gdt_entry_t(uint32_t base, uint32_t limit,
                        Descriptor access_flags,
                        Granularity gran_flags) noexcept
      : limit_low(limit & 0xFFFF),
        base_low(base & 0xFFFF),
        base_mid((base >> 16) & 0xFF),
        access(kstd::to_underlying(access_flags)),
        granularity(((limit >> 16) & 0x0F) |
                    kstd::to_underlying(gran_flags)),
        base_high((base >> 24) & 0xFF) {}
};

static_assert(sizeof(gdt_entry_t) == 8);
static_assert(sizeof(gdtr_t) == 6);
static_assert(sizeof(const gdt_entry_t *) == 4);

// endregion

void init_gdt();
}// namespace kernel::gdt
