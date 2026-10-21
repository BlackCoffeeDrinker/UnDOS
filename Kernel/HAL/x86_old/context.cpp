#include <kernel/hal_interface.hpp>

#include "tss.hpp"

// Low-level x86 (i386) CPU context primitives for the platform-agnostic
// scheduler. The kernel core stores only a saved stack pointer per thread;
// here we frame new kernel stacks and swap the callee-saved register set + ESP.

UNDOS_HAL_API_DEF void HAL_CPU_EnableInterrupts() noexcept {
  __asm__ volatile("sti" ::: "memory");
}

UNDOS_HAL_API_DEF void HAL_CPU_DisableInterrupts() noexcept {
  __asm__ volatile("cli" ::: "memory");
}

// Build the initial kernel stack for a brand-new thread so that the first
// HAL_CPU_SwitchContext into it pops zeroed callee-saved registers and then
// "returns" (ret) into `trampoline`.
//
// Stack layout built downward from the 16-byte aligned top:
//   [top-4 ]  padding (acts as trampoline's unused return slot; keeps 16-align)
//   [top-8 ]  trampoline address        <- popped by `ret`
//   [top-12]  saved EBX (0)
//   [top-16]  saved ESI (0)
//   [top-20]  saved EDI (0)
//   [top-24]  saved EBP (0)             <- the returned saved stack pointer
//
// With this layout the trampoline runs with (ESP+4) 16-byte aligned, matching
// the i386 System V ABI expected at a function entry point.
UNDOS_HAL_API_DEF kernel::VirtualAddress HAL_CPU_InitThreadContext(
    kernel::VirtualAddress stack_top,
    kernel::cfunc<void()> trampoline) noexcept {
  const auto top = static_cast<uintptr_t>(stack_top.value) & ~static_cast<uintptr_t>(0xF);
  auto *sp = reinterpret_cast<uint32_t *>(top);

  *(--sp) = 0;                                                  // padding / trampoline return slot
  *(--sp) = reinterpret_cast<uintptr_t>(trampoline.fn);         // ret target -> trampoline
  *(--sp) = 0;                                                  // EBX
  *(--sp) = 0;                                                  // ESI
  *(--sp) = 0;                                                  // EDI
  *(--sp) = 0;                                                  // EBP

  return {reinterpret_cast<uintptr_t>(sp)};
}

// Tiny trampoline used as the "return address" HAL_CPU_SwitchContext lands on
// the very first time a brand-new ring-3 thread is scheduled in. By the time
// `ret` jumps here, ESP already points straight at the iret frame built by
// HAL_CPU_InitUserThreadContext below, so all that is left to do is `iret`.
extern "C" void hal_x86_ring3_entry_trampoline();
__asm__(
    ".text\n\t"
    ".global hal_x86_ring3_entry_trampoline\n\t"
    ".type hal_x86_ring3_entry_trampoline, @function\n\t"
    "hal_x86_ring3_entry_trampoline:\n\t"
    "    iret\n\t"
    ".size hal_x86_ring3_entry_trampoline, . - hal_x86_ring3_entry_trampoline\n\t");

// Build a brand-new kernel stack that, once loaded via HAL_CPU_SwitchContext,
// pops zeroed callee-saved registers and then `ret`s straight into an `iret`
// that drops the CPU into ring 3 at `user_entry` with `user_stack_top` as ESP
// and interrupts enabled (matching HAL_CPU_InitThreadContext's stack shape,
// but with an iret frame beneath the trampoline instead of a plain return).
UNDOS_HAL_API_DEF kernel::VirtualAddress HAL_CPU_InitUserThreadContext(
    kernel::VirtualAddress kernel_stack_top,
    kernel::VirtualAddress user_entry,
    kernel::VirtualAddress user_stack_top) noexcept {
  const auto top = static_cast<uintptr_t>(kernel_stack_top.value) & ~static_cast<uintptr_t>(0xF);
  auto *sp = reinterpret_cast<uint32_t *>(top);

  constexpr uint32_t user_code_selector = hal::x86::GDT_SELECTOR_USER_CODE | 3;
  constexpr uint32_t user_data_selector = hal::x86::GDT_SELECTOR_USER_DATA | 3;
  constexpr uint32_t eflags_if = 0x202;// reserved bit 1 + IF

  *(--sp) = user_data_selector;                                     // SS (ring 3)
  *(--sp) = static_cast<uint32_t>(user_stack_top.value);             // ESP (ring 3)
  *(--sp) = eflags_if;                                               // EFLAGS
  *(--sp) = user_code_selector;                                      // CS (ring 3)
  *(--sp) = static_cast<uint32_t>(user_entry.value);                 // EIP
  *(--sp) = reinterpret_cast<uintptr_t>(&hal_x86_ring3_entry_trampoline);// ret target
  *(--sp) = 0;                                                       // EBX
  *(--sp) = 0;                                                       // ESI
  *(--sp) = 0;                                                       // EDI
  *(--sp) = 0;                                                       // EBP

  return {reinterpret_cast<uintptr_t>(sp)};
}

// HAL_CPU_SwitchContext(VirtualAddress& save_sp_out, VirtualAddress load_sp)
//
// i386 cdecl at entry:  4(%esp) = &save_sp_out (pointer), 8(%esp) = load_sp value.
// Save callee-saved registers on the outgoing stack, store the outgoing ESP
// into *save_sp_out, load the incoming ESP, restore its callee-saved registers,
// and return into the incoming thread's saved return address.
__asm__(
    ".text\n\t"
    ".global HAL_CPU_SwitchContext\n\t"
    ".type HAL_CPU_SwitchContext, @function\n\t"
    "HAL_CPU_SwitchContext:\n\t"
    "    movl 4(%esp), %eax\n\t"   // eax = &save_sp_out
    "    movl 8(%esp), %edx\n\t"   // edx = load_sp
    "    pushl %ebx\n\t"
    "    pushl %esi\n\t"
    "    pushl %edi\n\t"
    "    pushl %ebp\n\t"
    "    movl %esp, (%eax)\n\t"    // *save_sp_out = current ESP
    "    movl %edx, %esp\n\t"      // load incoming ESP
    "    popl %ebp\n\t"
    "    popl %edi\n\t"
    "    popl %esi\n\t"
    "    popl %ebx\n\t"
    "    ret\n\t"
    ".size HAL_CPU_SwitchContext, . - HAL_CPU_SwitchContext\n\t");
