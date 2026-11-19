
#include "stdkrn.hpp"

#include <kernel/time.hpp>

#include "scheduler/Scheduler.hpp"
#include "wait.hpp"

namespace {
// Until the HAL reports its rate, assume the PIT power-on default
// (1193182 Hz / 65536 ~= 18.2 Hz).
uint64_t g_increment_ns = 54925439;
uint64_t g_system_time_ns = 0;
}// namespace

// Called by the HAL to tell the Executive how much time passes per tick
UNDOS_KERNEL_API_DEF void KE_TIME_SetIncrement(kstd::chrono::nanoseconds nanoseconds) noexcept {
  g_increment_ns = static_cast<uint64_t>(nanoseconds.count());
}

// The core interrupt callback
UNDOS_KERNEL_API_DEF void KE_TIME_UpdateSystemTime() noexcept {
  // Increment by the actual physical duration of the interval
  g_system_time_ns += g_increment_ns;

  // Release timed waits whose deadline passed (interrupts are off here).
  kernel::wait::tick(KE_TIME_GetSystemTime());

  // Drive preemption: decrement the running thread's quantum and rotate when it
  // expires. The scheduler is a no-op until it has been armed.
  kernel::sched::tick();
}

// Public API for drivers/kernel to get the true elapsed time in milliseconds
UNDOS_KERNEL_API_DEF kstd::chrono::nanoseconds KE_TIME_GetSystemTime() noexcept {
  return kstd::chrono::nanoseconds(static_cast<long long>(g_system_time_ns));
}
