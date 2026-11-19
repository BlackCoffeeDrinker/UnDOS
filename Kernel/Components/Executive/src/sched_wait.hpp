#pragma once

#include <kernel/kerror.hpp>

namespace kernel {
struct KThreadObject;
struct KDispatcherHeader;
}// namespace kernel

// Wait/wake services the interrupt dispatcher needs from the thread scheduler.
// Declared here so dispatch.* does not depend on the scheduler; implemented on
// the scheduler side (wait_interface.cpp) and by the hosted test stubs.
namespace kernel::sched::wait {

/// Block the calling thread until `header` is signalled (consumes the signal).
kstd::error_code block_current(KDispatcherHeader &header) noexcept;

/// Ready the thread blocked on `header`, or leave the signal sticky. Safe from
/// an interrupt gate and before any thread waits.
void wake(KDispatcherHeader &header) noexcept;

/// Give up the CPU, re-queuing the calling thread.
void yield_current() noexcept;

/// The calling thread, or nullptr before the scheduler runs. Identity only.
KThreadObject *current_thread() noexcept;

/// Create and ready the Realtime dispatch worker running `entry(nullptr)`.
/// The scheduler side keeps the thread reference.
kstd::error_code ready_worker(void (*entry)(void *)) noexcept;

}// namespace kernel::sched::wait
