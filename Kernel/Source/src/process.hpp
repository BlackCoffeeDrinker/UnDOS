
#pragma once

#include "stdkrn.hpp"

namespace kernel::process {
// Terminates the calling thread's owning process. The process is marked
// Exiting and its other threads are woken from alertable waits; each of them
// retires itself the next time it enters or leaves the kernel (deferred kill).
// The last thread out notifies subscribers, signals the exit event, tears
// down the AddressSpace and removes the process. Never returns.
[[noreturn]] void terminate_current(int32_t code) noexcept;

// Retires the calling user thread with `code`. If it is the process's last
// thread, the process exits with `code` too. Never returns.
[[noreturn]] void exit_current_thread(uint32_t code) noexcept;

// Registers a new user thread with its process before it first runs.
// Fails with OutOfMemory when the process thread table is full, or
// NoCurrentProcess when the process is already exiting.
kstd::error_code attach_thread(KProcessObject &process, KThreadObject &thread) noexcept;
}// namespace kernel::process
