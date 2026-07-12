#pragma once

#include <strfmt.hpp>
#include <string_view.hpp>
#include <utility.hpp>

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/entry.hpp>
#include <kernel/memory/address.hpp>
#include <kernel/virtual_memory.hpp>

/**
 * @ingroup PLATFORM
 * @brief Method KE_PLATFORM_Init
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_PLATFORM_Init, const kernel::BootInfoT &boot_info);

/**
 * @ingroup PLATFORM
 * @brief Method KE_PLATFORM_InitializeSystemTimer
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_PLATFORM_InitializeSystemTimer);

/**
 * @ingroup PLATFORM
 * @brief Method KE_PLATFORM_AfterObjectManager
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_PLATFORM_AfterObjectManager);

/**
 * @ingroup CPU
 * @brief Method KE_CPU_Halt
 * 
 * Put CPU into low-power mode, wait for interrupt before returning
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_CPU_Halt);

/**
 * @ingroup PLATFORM
 * @brief Method KE_PLATFORM_Panic
 *
 */
UNDOS_HAL_PUBLIC_V1API([[noreturn]] void, HAL_PLATFORM_Panic, const char *message, const char *file, int line);

/**
 * @ingroup PLATFORM
 * @brief Method KE_PLATFORM_Shutdown
 *
 */
UNDOS_HAL_PUBLIC_V1API([[noreturn]] void, HAL_PLATFORM_Shutdown);

// region Port I/O API
/**
 * @ingroup IO
 * @brief Method KE_IO_Out8
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_IO_Out8, uint16_t port, uint8_t val);

/**
 * @ingroup IO
 * @brief Method KE_IO_In8
 *
 */
UNDOS_HAL_PUBLIC_V1API(uint8_t, HAL_IO_In8, uint16_t port);

/**
 * @ingroup IO
 * @brief Method KE_IO_Out16
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_IO_Out16, uint16_t port, uint16_t val);

/**
 * @ingroup IO
 * @brief Method KE_IO_In16
 *
 */
UNDOS_HAL_PUBLIC_V1API(uint16_t, HAL_IO_In16, uint16_t port);

/**
 * @ingroup IO
 * @brief Method KE_IO_Out32
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_IO_Out32, uint16_t port, uint32_t val);

/**
 * @ingroup IO
 * @brief Method KE_IO_In32
 *
 */
UNDOS_HAL_PUBLIC_V1API(uint32_t, HAL_IO_In32, uint16_t port);

/**
 * @ingroup IO
 * @brief Method KE_IO_Delay
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_IO_Delay);
// endregion

// region CPU Execution Environment API
/**
 * @ingroup PLATFORM
 * @brief Method KE_PLATFORM_GetCpuCount
 *
 */
UNDOS_HAL_PUBLIC_V1API(uint32_t, HAL_PLATFORM_GetCpuCount);

/**
 * @ingroup CPU
 * @brief Method KE_CPU_EnableInterrupts
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_CPU_EnableInterrupts);

/**
 * @ingroup CPU
 * @brief Method KE_CPU_DisableInterrupts
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_CPU_DisableInterrupts);

/**
 * @ingroup CPU
 * @brief Method KE_CPU_InitThreadContext
 *
 * Frame a brand-new kernel stack so the first HAL_CPU_SwitchContext into it
 * "returns" through `trampoline`. Returns the initial saved stack pointer to
 * store in KThreadObject::kernel_stack_top.
 */
UNDOS_HAL_PUBLIC_V1API(
    kernel::VirtualAddress,
    HAL_CPU_InitThreadContext,
    kernel::VirtualAddress stack_top, kernel::cfunc<void()> trampoline);

/**
 * @ingroup CPU
 * @brief Method KE_CPU_SwitchContext
 *
 * Save the current CPU context, write the outgoing stack pointer into
 * `save_sp_out`, then load the context previously saved at `load_sp`.
 */
UNDOS_HAL_PUBLIC_V1API(
    void,
    HAL_CPU_SwitchContext,
    kernel::VirtualAddress &save_sp_out, kernel::VirtualAddress load_sp);

/**
 * @ingroup CPU
 * @brief Method HAL_CPU_InitUserThreadContext
 *
 * Frame a brand-new kernel stack so the first HAL_CPU_SwitchContext into it
 * pops straight into an `iret` that drops the CPU into ring 3 at
 * `user_entry` with `user_stack_top` loaded as ESP and interrupts enabled.
 * Returns the initial saved stack pointer for KThreadObject::kernel_stack_top.
 */
UNDOS_HAL_PUBLIC_V1API(
    kernel::VirtualAddress,
    HAL_CPU_InitUserThreadContext,
    kernel::VirtualAddress kernel_stack_top, kernel::VirtualAddress user_entry, kernel::VirtualAddress user_stack_top);

/**
 * @ingroup CPU
 * @brief Method HAL_CPU_SetKernelStack
 *
 * Updates the TSS esp0 field so a ring-3 -> ring-0 transition (syscall,
 * IRQ, exception) for the about-to-run thread lands on its own kernel stack.
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_CPU_SetKernelStack, kernel::VirtualAddress kernel_stack_top);
// endregion

// region Debug
UNDOS_HAL_PUBLIC_V1API(void, early_print, const char *);
UNDOS_HAL_PUBLIC_V1API(void, early_print_char, char);

template<typename... Args>
void early_print_fmt(const kstd::string_view &fmt, Args &&...args) {
  kstd::format_dst([](char c) { early_print_char(c); }, fmt, args...);
}
// endregion

// region Physical Memory Manager API
/**
 * @ingroup PMM
 * @brief Method KE_PMM_AllocateFrames
 *
 */
UNDOS_HAL_PUBLIC_V1API(kernel::PhysicalAddress, HAL_PMM_AllocateFrames, size_t count);

/**
 * @ingroup PMM
 * @brief Method KE_PMM_AllocateFramesDMA
 *
 */
UNDOS_HAL_PUBLIC_V1API(kernel::PhysicalAddress, HAL_PMM_AllocateFramesDMA, size_t count);

/**
 * @ingroup PMM
 * @brief Method KE_PMM_FreeFrames
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_PMM_FreeFrames, kernel::PhysicalAddress base, size_t count);

/**
 * @ingroup PMM
 * @brief Method KE_PMM_ReserveRegion
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_PMM_ReserveRegion, kernel::PhysicalAddress base, size_t length);
// endregion

// region Virtual Memory Manager API
/**
 * @ingroup VMM
 * @brief Method KE_VMM_GetHighestVirtualAddress
 *
 */
UNDOS_HAL_PUBLIC_V1API(kernel::VirtualAddress, HAL_VMM_GetHighestVirtualAddress);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_FinalizeInit
 * 
 * Object Manager is available here
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_VMM_FinalizeInit);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_GetCurrentTranslationRoot
 *
 */
UNDOS_HAL_PUBLIC_V1API(kernel::PhysicalAddress, HAL_VMM_GetCurrentTranslationRoot);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_GetPhysicalAddress
 * 
 * Walks the active page structures to return the raw physical frame.
 */
UNDOS_HAL_PUBLIC_V1API(kernel::PhysicalAddress, HAL_VMM_GetPhysicalAddress, kernel::VirtualAddress virt);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_SwitchAddressSpace
 * 
 * Loads a new translation root (e.g. CR3, TTBR, satp).
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_VMM_SwitchAddressSpace, kernel::PhysicalAddress new_root);

/**
 * @ingroup VMM
 * @brief Method HAL_VMM_CreateAddressSpace
 *
 * Allocates a brand-new translation root (e.g. a fresh page directory) with
 * the kernel's higher-half mappings cloned/shared, and an empty user half.
 */
UNDOS_HAL_PUBLIC_V1API(kernel::PhysicalAddress, HAL_VMM_CreateAddressSpace);

/**
 * @ingroup VMM
 * @brief Method HAL_VMM_DestroyAddressSpace
 *
 * Frees the page-table frames owned by a translation root previously
 * returned by HAL_VMM_CreateAddressSpace.
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_VMM_DestroyAddressSpace, kernel::PhysicalAddress root);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_MapPage
 *
 */
UNDOS_HAL_PUBLIC_V1API(bool, HAL_VMM_MapPage, kernel::VirtualAddress virt, kernel::PhysicalAddress phys, kernel::vmm::ProtectFlags flags);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_UnmapPage
 *
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_VMM_UnmapPage, kernel::VirtualAddress virt);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_Flush
 * Reloads CR3
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_VMM_Flush, kernel::VirtualAddress virt);
// endregion

// region Atomic Operations
enum class PlatformMemoryOrder : uint8_t {
  Relaxed,
  Consume,
  Acquire,
  Release,
  AcquireRelease,
  SequentiallyConsistent
};

// endregion
