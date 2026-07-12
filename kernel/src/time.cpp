
#include "stdkrn.hpp"
#include <tuple.hpp>

#include <kernel/time.hpp>

#include "scheduler/Scheduler.hpp"

// Called by the HAL to tell the Executive how much time passes per tick
UNDOS_KERNEL_API_DEF void KE_TIME_SetIncrement(kstd::chrono::nanoseconds nanoseconds) noexcept {
  kstd::ignore = nanoseconds;
}

// The core interrupt callback
UNDOS_KERNEL_API_DEF void KE_TIME_UpdateSystemTime() noexcept {
  // Increment by the actual physical duration of the interval

  // Drive preemption: decrement the running thread's quantum and rotate when it
  // expires. The scheduler is a no-op until it has been armed.
  kernel::sched::tick();
}

// Public API for drivers/kernel to get the true elapsed time in milliseconds
UNDOS_KERNEL_API_DEF kstd::chrono::milliseconds KE_TIME_GetSystemTime() noexcept {
  return {};
}
