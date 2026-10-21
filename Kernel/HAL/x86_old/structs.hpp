
#ifndef UNDOS_STRUCTS_HPP
#define UNDOS_STRUCTS_HPP

#include <stddef.h>
#include <stdint.h>

#include <type_traits.hpp>
#include <utility.hpp>

#include <kernel/hal_interface.hpp>

namespace hal::x86 {
template<typename E>
concept Enum8 = kstd::is_enum_v<E> && sizeof(E) == 1;

template<Enum8 E>
constexpr E operator|(E a, E b) noexcept { return static_cast<E>(kstd::to_underlying(a) | kstd::to_underlying(b)); }

template<Enum8 E>
constexpr E operator&(E a, E b) noexcept { return static_cast<E>(kstd::to_underlying(a) & kstd::to_underlying(b)); }

template<Enum8 E>
constexpr E operator~(E a) noexcept { return static_cast<E>(~kstd::to_underlying(a)); }

template<Enum8 E>
constexpr bool has_flag(E mask, E flag) noexcept { return (mask & flag) == flag; }


struct [[gnu::packed]] regs {
  uint32_t edi;
  uint32_t esi;
  uint32_t ebp;
  uint32_t esp;
  uint32_t ebx;
  uint32_t edx;
  uint32_t ecx;
  uint32_t eax;
  uint32_t ds;
  uint32_t es;
  uint32_t fs;
  uint32_t gs;
  uint32_t which_int;
  uint32_t err_code;
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
  uint32_t user_esp;
  uint32_t user_ss;
};

struct [[gnu::packed]] tss {
  uint32_t prev_tss;// The previous TSS - with hardware task switching these form a kind of backward linked list.
  uint32_t esp0;    // The stack pointer to load when changing to kernel mode.
  uint32_t ss0;     // The stack segment to load when changing to kernel mode.
  // Everything below here is unused.
  uint32_t esp1;// esp and ss 1 and 2 would be used when switching to rings 1 or 2.
  uint32_t ss1;
  uint32_t esp2;
  uint32_t ss2;
  uint32_t cr3;
  uint32_t eip;
  uint32_t eflags;
  uint32_t eax;
  uint32_t ecx;
  uint32_t edx;
  uint32_t ebx;
  uint32_t esp;
  uint32_t ebp;
  uint32_t esi;
  uint32_t edi;
  uint32_t es;
  uint32_t cs;
  uint32_t ss;
  uint32_t ds;
  uint32_t fs;
  uint32_t gs;
  uint32_t ldt;
  uint16_t trap;
  uint16_t iomap_base;
};

struct X86_Task {
  uint32_t esp;           // Saved stack pointer
  uint32_t ebp;           // Saved base pointer
  uint32_t eip;           // Instruction pointer
  uint32_t registers[8];  // General-purpose registers
  uint32_t kernel_stack;  // Kernel-mode stack pointer
  uint32_t page_directory;// Task-specific page directory
  uint8_t privilege_level;// 0 = Kernel, 3 = User
  uint16_t cs;            // Code segment
  uint16_t ds;            // Data segment
  uint16_t ss;            // Stack segment
};
}// namespace kernel::x86
#endif//UNDOS_STRUCTS_HPP
