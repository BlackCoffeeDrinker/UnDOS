
#include <kernel/time.hpp>
#include <tuple.hpp>

namespace hal::time {
// Called by the HAL to tell the Executive how much time passes per tick
UNDOS_KERNEL_API void KE_Time_SetIncrement(kstd::chrono::nanoseconds nanoseconds) noexcept {
  kstd::ignore = nanoseconds;
}

// The core interrupt callback
UNDOS_KERNEL_API void KE_Time_UpdateSystemTime() noexcept {
  // Increment by the actual physical duration of the interval

  // Thread quantum management, timers, etc. happen downstream
}

// Public API for drivers/kernel to get the true elapsed time in milliseconds
UNDOS_KERNEL_API kstd::chrono::milliseconds KE_Time_GetSystemTime() noexcept {
  return {};
}
}// namespace kernel::time
