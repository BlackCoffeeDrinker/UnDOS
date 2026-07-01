
#pragma once

#include <stdint.h>

#include "structs.hpp"

namespace hal::x86 {
enum class GateType : uint8_t {
  TASK = 0b0101,
  INTERRUPT_16 = 0b0110,
  TRAP_16 = 0b0111,
  INTERRUPT_32 = 0b1110,// What we will use for almost everything
  TRAP_32 = 0b1111
};

enum class IDTFlags : uint8_t {
  DPL0 = 0b00000000,
  DPL3 = 0b01100000,
  PRESENT = 0b10000000
};

enum class IDTAttr : uint8_t {};

constexpr IDTAttr operator|(GateType a, IDTFlags b) noexcept {
  return static_cast<IDTAttr>(
      kstd::to_underlying(a) | kstd::to_underlying(b));
}

struct [[gnu::packed]] idt_entry_t {
  uint16_t isr_low;  // The lower 16 bits of the ISR's address
  uint16_t kernel_cs;// The GDT selector for our kernel code (0x08)
  uint8_t reserved;  // Set to 0
  uint8_t attributes;// Gate type, DPL, and Present bit
  uint16_t isr_high; // The higher 16 bits of the ISR's address

  constexpr idt_entry_t() noexcept : isr_low(0), kernel_cs(0), reserved(0), attributes(0), isr_high(0) {}

  constexpr idt_entry_t(uint32_t offset_, uint16_t select_, GateType type, IDTFlags flags)
      : isr_low(static_cast<uint16_t>(offset_ & 0xffff)),
        kernel_cs(select_),
        reserved(0),
        attributes(static_cast<uint8_t>(type | flags)),
        isr_high(static_cast<uint16_t>((offset_ & 0xffff0000) >> 16)) {
  }

  void set_handler(uint32_t isr_address, uint16_t selector, GateType type, IDTFlags flags) noexcept {
    isr_low = static_cast<uint16_t>(isr_address & 0xFFFF);
    kernel_cs = selector;
    reserved = 0;
    attributes = static_cast<uint8_t>(type | flags);
    isr_high = static_cast<uint16_t>((isr_address >> 16) & 0xFFFF);
  }
};

struct [[gnu::packed]] idtr_t {
  uint16_t limit;
  uint32_t base;
};

struct stack_frame {
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
};

template<size_t N>
struct ISR {
  static_assert(N <= 256, "Too many ISRs");

  enum {
    IDT_MAX_DESCRIPTORS = N,
  };

  [[gnu::aligned(0x10)]] idt_entry_t idt[N];
  idtr_t idtr;

  // !!!!
  // DO NOT REMOVE THIS CONSTEXPR
  // !!!!
  constexpr ISR() : idt{},
                    idtr(sizeof(idt_entry_t) * N - 1, 0) {
  }

  void set(uint8_t n, uint16_t selector, [[gnu::interrupt]] void (*isr)(struct stack_frame *, unsigned int), GateType type, IDTFlags flags) {
    idt[n] = idt_entry_t(reinterpret_cast<uintptr_t>(isr), selector, type, flags);
  }

  void set(uint8_t n, uint16_t selector, [[gnu::interrupt]] void (*isr)(struct stack_frame *), GateType type, IDTFlags flags) {
    idt[n] = idt_entry_t(reinterpret_cast<uintptr_t>(isr), selector, type, flags);
  }

  [[gnu::always_inline]] void set() {
    idtr.base = reinterpret_cast<uintptr_t>(&idt[0]);
    __asm__ volatile("lidt %0" : : "m"(idtr));// load the new IDT
  }
};

static_assert(sizeof(idt_entry_t) == 8);
static_assert(sizeof(idtr_t) == 6);

void init_idt();

}// namespace hal::x86
