#pragma once

#include <kernel/__core.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/virtual_memory.hpp>

#include <chrono.hpp>

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

/**
 * @ingroup SCHED
 * @brief Method KE_SCHED_UnblockFromIsr
 * 
 * Return a blocked thread to the ready queue without modifying CPU interrupt state.
 * Safe to call from hardware interrupt service routines (DIRQL).
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_SCHED_UnblockFromIsr,
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

namespace kernel {
struct KDispatcherHeader;
struct KEventObject;

enum class WaitStatus : uint32_t {
  Signalled = 0,
  Timeout,
  Invalid,
};

// Infinite timeout for KE_SCHED_WaitForObject.
constexpr kstd::chrono::milliseconds WAIT_INFINITE{-1};
} // namespace kernel

/**
 * @ingroup SCHED
 * @brief Method KE_SCHED_WaitForObject
 *
 * Block the current thread until the waitable `object` (event or process) is
 * signalled or `timeout` elapses. A zero timeout polls; WAIT_INFINITE never
 * times out.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::WaitStatus,
    KE_SCHED_WaitForObject,
    const kernel::KObjectPtr<kernel::KObject> &object, kstd::chrono::milliseconds timeout);

/**
 * @ingroup SCHED
 * @brief Method KE_SCHED_WaitForHeader
 *
 * Same as KE_SCHED_WaitForObject for a header embedded in a kernel structure.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::WaitStatus,
    KE_SCHED_WaitForHeader,
    kernel::KDispatcherHeader &header, kstd::chrono::milliseconds timeout);

/**
 * @ingroup SCHED
 * @brief Method KE_EVENT_Signal
 *
 * Signal a dispatcher header and ready its waiters. Not ISR-safe: it toggles
 * interrupts. Use KE_EVENT_SignalFromIsr from interrupt context.
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_EVENT_Signal, kernel::KDispatcherHeader &header, bool wakeAll);

/**
 * @ingroup SCHED
 * @brief Method KE_EVENT_SignalFromIsr
 *
 * KE_EVENT_Signal for callers that already run with interrupts disabled.
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_EVENT_SignalFromIsr, kernel::KDispatcherHeader &header, bool wakeAll);

/**
 * @ingroup SCHED
 * @brief Method KE_EVENT_Create
 *
 * Create an unnamed event object.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KEventObject>,
    KE_EVENT_Create,
    bool manualReset, bool initialState);
