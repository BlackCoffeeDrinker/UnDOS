#pragma once

#include <kernel/__core.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/virtual_memory.hpp>

// Platform-agnostic thread scheduler service. The core owns the policy (ready
// queue, current thread, scheduling decision); all CPU-specific work is done
// through the HAL_CPU_* context primitives declared in hal_interface.hpp.


/**
 * @ingroup SCHED
 * @brief Method KE_SCHED_CreateThread
 *
 * Create a ready thread with its own kernel stack. `arg` is passed opaquely to
 * `entry`. Returns the new thread object, or a null pointer on failure.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KThreadObject>,
    KE_SCHED_CreateThread,
    kernel::cfunc<void(void *)> entry, void *arg, uint8_t priority);

/**
 * @ingroup SCHED
 * @brief Method KE_SCHED_CreateUserThread
 *
 * Create a ready ring-3 thread whose translation_root is `as.translation_root`,
 * starting execution at `user_entry` with `user_stack_top` loaded as ESP.
 * Uses HAL_CPU_InitUserThreadContext instead of the kernel-only init path.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KThreadObject>,
    KE_SCHED_CreateUserThread,
    const kernel::vmm::AddressSpace &as, kernel::VirtualAddress user_entry, kernel::VirtualAddress user_stack_top);

/**
 * @ingroup SCHED
 * @brief Method KE_SCHED_Yield
 *
 * Voluntarily give up the CPU, re-queuing the current thread at the back.
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_SCHED_Yield);

/**
 * @ingroup SCHED
 * @brief Method KE_SCHED_Block
 * 
 * Move the current thread out of the ready queue until it is unblocked.
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_SCHED_Block);

/**
 * @ingroup SCHED
 * @brief Method KE_SCHED_Unblock
 * 
 * Return a blocked thread to the ready queue.
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_SCHED_Unblock,
    const kernel::KObjectPtr<kernel::KThreadObject> &thread);

// Retire the current thread permanently and switch away.
UNDOS_KERNEL_PUBLIC_V1API([[noreturn]] void, KE_SCHED_Terminate);

/**
 * @ingroup SCHED
 * @brief Method KE_SCHED_GetCurrentThread
 * 
 * The currently running thread (null before the scheduler is started).
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KThreadObject>, KE_SCHED_GetCurrentThread);
