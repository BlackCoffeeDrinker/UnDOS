// Uniprocessor implementations of the SMP / CPU-identity HAL surface.
//
// x86_old targets pre-ACPI machines: there is no MP table, no local APIC and
// therefore exactly one logical CPU. The calls below give the kernel core a
// complete, well-defined SMP surface on such a machine without any of the
// existing x86_old sources having to change.

#include <kernel/hal_interface.hpp>

UNDOS_HAL_API_DEF uint32_t HAL_CPU_GetCurrentId() noexcept {
  return 0;
}

UNDOS_HAL_API_DEF uintptr_t HAL_CPU_SaveAndDisableInterrupts() noexcept {
  uintptr_t flags;
  asm volatile("pushfl\n\tpopl %0\n\tcli"
               : "=r"(flags)
               :
               : "memory");
  return flags;
}

UNDOS_HAL_API_DEF void HAL_CPU_RestoreInterrupts(const uintptr_t state) noexcept {
  // Only re-enable when the saved EFLAGS had IF set, so a restore nested
  // inside an already-disabled region leaves interrupts disabled.
  if (state & 0x200u) {
    asm volatile("sti" ::: "memory");
  }
}

UNDOS_HAL_API_DEF bool HAL_SMP_StartProcessor(uint32_t cpu_id,
                                              kernel::cfunc<void(uint32_t)> entry,
                                              kernel::VirtualAddress stack_top) noexcept {
  (void) cpu_id;
  (void) entry;
  (void) stack_top;
  // No application processor exists on this platform.
  return false;
}

UNDOS_HAL_API_DEF void HAL_SMP_SendRescheduleIpi(uint32_t cpu_id) noexcept {
  (void) cpu_id;
}

UNDOS_HAL_API_DEF void HAL_SMP_ShootdownTlb(const kernel::VirtualAddress addr) noexcept {
  HAL_VMM_Flush(addr);
}
