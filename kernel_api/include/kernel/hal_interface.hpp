#pragma once

#include <strfmt.hpp>
#include <string_view.hpp>
#include <utility.hpp>

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/entry.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KInterruptServiceRoutineObject.hpp>
#include <kernel/memory/address.hpp>
#include <kernel/virtual_memory.hpp>

// region Platform API
/**
 * @ingroup PLATFORM
 * @brief Method KE_PLATFORM_Init
 *
 */
UNDOS_HAL_PUBLIC_V1API(void,
                       HAL_PLATFORM_Init,
                       const kernel::BootInfoT &boot_info,
                       kernel::vmm::AddressSpace &kernel_address_space);

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
UNDOS_HAL_PUBLIC_V1API([[noreturn]] void, HAL_PLATFORM_Panic, const kstd::string_view &message, const char *file, int line);

/**
 * @ingroup PLATFORM
 * @brief Method KE_PLATFORM_Shutdown
 *
 */
UNDOS_HAL_PUBLIC_V1API([[noreturn]] void, HAL_PLATFORM_Shutdown);
// endregion

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

// region Interrupt API
/**
 * @ingroup INTERRUPT
 * @brief Method HAL_INTR_ConnectInterrupt
 *
 * Connects an already-constructed KInterruptServiceRoutineObject to the
 * given platform-specific interrupt source. The HAL defines the valid
 * identifier range and controller behavior; the identifier is also used
 * in queued interrupt records to select kernel handlers.
 */
UNDOS_HAL_PUBLIC_V1API(
    bool,
    HAL_INTR_ConnectInterrupt,
    uint32_t irq,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr);

/**
 * @ingroup INTERRUPT
 * @brief Method HAL_INTR_DisconnectInterrupt
 *
 * Detaches whatever KInterruptServiceRoutineObject is connected to the
 * given platform-specific interrupt source.
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_INTR_DisconnectInterrupt, uint32_t irq);

/**
 * @ingroup INTERRUPT
 * @brief Method HAL_INTR_SetIrqMask
 *
 * Masks or unmasks a single hardware IRQ line (0-15) on the appropriate
 * PIC via a read-modify-write of its Interrupt Mask Register.
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_INTR_SetIrqMask, uint32_t irq, bool masked);
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
 * @brief Method HAL_CPU_EnableInterrupts
 *
 * Unconditionally enables interrupts. Reserved for the one-way transition a
 * CPU makes when it enters the scheduler for the first time (BSP boot, AP
 * bring-up) and for HAL-internal use. Every other caller must use
 * HAL_CPU_SaveAndDisableInterrupts / HAL_CPU_RestoreInterrupts, which nest
 * correctly.
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_CPU_EnableInterrupts);

/**
 * @ingroup CPU
 * @brief Method HAL_CPU_DisableInterrupts
 *
 * Unconditionally disables interrupts, discarding the previous state.
 * HAL-internal only; kernel code uses
 * HAL_CPU_SaveAndDisableInterrupts / HAL_CPU_RestoreInterrupts instead.
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

/**
 * @ingroup CPU
 * @brief Method HAL_CPU_GetKernelStack
 *
 * Returns the ring-0 stack currently configured for ring-3/v86 -> ring-0
 * transitions on this CPU. The scheduler saves it when switching a thread
 * out so temporary per-thread overrides (e.g. a v86 BIOS call's private trap
 * stack) survive preemption.
 */
UNDOS_HAL_PUBLIC_V1API(kernel::VirtualAddress, HAL_CPU_GetKernelStack);

/**
 * @ingroup CPU
 * @brief Method HAL_CPU_GetCurrentId
 *
 * Identifies the logical CPU executing the call. Must be valid everywhere,
 * including inside an ISR and on a CPU that has just been started. A
 * uniprocessor HAL returns 0.
 */
UNDOS_HAL_PUBLIC_V1API(uint32_t, HAL_CPU_GetCurrentId);

/**
 * @ingroup CPU
 * @brief Method HAL_CPU_SaveAndDisableInterrupts
 *
 * Captures the current interrupt-enable state, disables interrupts, and
 * returns an opaque token to hand back to HAL_CPU_RestoreInterrupts. Nesting
 * is supported: restoring a token captured while interrupts were already
 * disabled leaves them disabled.
 */
UNDOS_HAL_PUBLIC_V1API(uintptr_t, HAL_CPU_SaveAndDisableInterrupts);

/**
 * @ingroup CPU
 * @brief Method HAL_CPU_RestoreInterrupts
 *
 * Restores the interrupt-enable state captured by a previous
 * HAL_CPU_SaveAndDisableInterrupts call.
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_CPU_RestoreInterrupts, uintptr_t state);
// endregion

// region SMP API
/**
 * @ingroup CPU
 * @brief Method HAL_SMP_StartProcessor
 *
 * Brings logical CPU `cpu_id` out of reset so it enters `entry` with
 * `stack_top` as its kernel stack and its own CPU id as the argument.
 * Returns false when the platform cannot start that CPU; a uniprocessor HAL
 * always returns false, which is a supported outcome rather than an error.
 */
UNDOS_HAL_PUBLIC_V1API(
    bool,
    HAL_SMP_StartProcessor,
    uint32_t cpu_id, kernel::cfunc<void(uint32_t)> entry, kernel::VirtualAddress stack_top);

/**
 * @ingroup CPU
 * @brief Method HAL_SMP_SendRescheduleIpi
 *
 * Nudges a remote CPU so it re-runs the scheduler. A no-op on uniprocessor
 * HALs and when `cpu_id` names the calling CPU.
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_SMP_SendRescheduleIpi, uint32_t cpu_id);

/**
 * @ingroup CPU
 * @brief Method HAL_SMP_ShootdownTlb
 *
 * Invalidates `addr` on every CPU that may have it cached. On uniprocessor
 * HALs this degrades to a local HAL_VMM_Flush.
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_SMP_ShootdownTlb, kernel::VirtualAddress addr);
// endregion

// region Debug
// This function is used to print debug messages to the firmware console. it should only be used for early boot stages.
UNDOS_HAL_PUBLIC_V1API(void, HAL_Debug_FirmwarePrint, const char *, size_t);
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
/**
 * @ingroup ATOMIC
 * @brief C++20-style memory order for atomic operations and fences
 */
enum class PlatformMemoryOrder : uint8_t {
  Relaxed,
  Consume,
  Acquire,
  Release,
  AcquireRelease,
  SequentiallyConsistent
};

/**
 * @ingroup ATOMIC
 * @brief Method HAL_ATOMIC_ThreadFence
 *
 * Implements std::atomic_thread_fence(order) semantics.
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_ATOMIC_ThreadFence, PlatformMemoryOrder order);

/**
 * @ingroup ATOMIC
 * @brief Method HAL_ATOMIC_Load32
 *
 * Atomically loads a 32-bit value with the given memory order.
 */
UNDOS_HAL_PUBLIC_V1API(
    uint32_t,
    HAL_ATOMIC_Load32,
    kernel::VirtualAddress addr,
    PlatformMemoryOrder order);

/**
 * @ingroup ATOMIC
 * @brief Method HAL_ATOMIC_Store32
 *
 * Atomically stores a 32-bit value with the given memory order.
 */
UNDOS_HAL_PUBLIC_V1API(
    void,
    HAL_ATOMIC_Store32,
    kernel::VirtualAddress addr,
    uint32_t value,
    PlatformMemoryOrder order);

/**
 * @ingroup ATOMIC
 * @brief Method HAL_ATOMIC_Exchange32
 *
 * Atomically exchanges a 32-bit value with the given memory order.
 */
UNDOS_HAL_PUBLIC_V1API(
    uint32_t,
    HAL_ATOMIC_Exchange32,
    kernel::VirtualAddress addr,
    uint32_t value,
    PlatformMemoryOrder order);

/**
 * @ingroup ATOMIC
 * @brief Method HAL_ATOMIC_CompareExchange32
 *
 * Atomically compares and exchanges a 32-bit value with the given memory order.
 * Returns true on success, false on failure; on failure, *expected_out is updated
 * with the current value.
 */
UNDOS_HAL_PUBLIC_V1API(
    bool,
    HAL_ATOMIC_CompareExchange32,
    kernel::VirtualAddress addr,
    uint32_t &expected_out,
    uint32_t desired,
    PlatformMemoryOrder order);

/**
 * @ingroup ATOMIC
 * @brief Method HAL_ATOMIC_FetchAdd32
 *
 * Atomically adds delta to a 32-bit value with the given memory order.
 * Returns the previous value.
 */
UNDOS_HAL_PUBLIC_V1API(
    uint32_t,
    HAL_ATOMIC_FetchAdd32,
    kernel::VirtualAddress addr,
    uint32_t delta,
    PlatformMemoryOrder order);

/**
 * @ingroup ATOMIC
 * @brief Method HAL_ATOMIC_Load64
 *
 * Atomically loads a 64-bit value with the given memory order.
 * May be emulated on CPUs without native 64-bit atomics.
 */
UNDOS_HAL_PUBLIC_V1API(
    uint64_t,
    HAL_ATOMIC_Load64,
    kernel::VirtualAddress addr,
    PlatformMemoryOrder order);

/**
 * @ingroup ATOMIC
 * @brief Method HAL_ATOMIC_Store64
 *
 * Atomically stores a 64-bit value with the given memory order.
 */
UNDOS_HAL_PUBLIC_V1API(
    void,
    HAL_ATOMIC_Store64,
    kernel::VirtualAddress addr,
    uint64_t value,
    PlatformMemoryOrder order);

/**
 * @ingroup ATOMIC
 * @brief Method HAL_ATOMIC_Exchange64
 *
 * Atomically exchanges a 64-bit value with the given memory order.
 */
UNDOS_HAL_PUBLIC_V1API(
    uint64_t,
    HAL_ATOMIC_Exchange64,
    kernel::VirtualAddress addr,
    uint64_t value,
    PlatformMemoryOrder order);

/**
 * @ingroup ATOMIC
 * @brief Method HAL_ATOMIC_CompareExchange64
 *
 * Atomically compares and exchanges a 64-bit value with the given memory order.
 * Returns true on success, false on failure; on failure, *expected_out is updated
 * with the current value.
 */
UNDOS_HAL_PUBLIC_V1API(
    bool,
    HAL_ATOMIC_CompareExchange64,
    kernel::VirtualAddress addr,
    uint64_t &expected_out,
    uint64_t desired,
    PlatformMemoryOrder order);
// endregion
