
#include <kernel/time.hpp>
#include <tuple.hpp>

// Called by the HAL to tell the Executive how much time passes per tick
UNDOS_KERNEL_API void KE_TIME_SetIncrement(kstd::chrono::nanoseconds nanoseconds) noexcept {
  kstd::ignore = nanoseconds;
}

// The core interrupt callback
UNDOS_KERNEL_API void KE_TIME_UpdateSystemTime() noexcept {
  // Increment by the actual physical duration of the interval

  // Thread quantum management, timers, etc. happen downstream
}

// Public API for drivers/kernel to get the true elapsed time in milliseconds
UNDOS_KERNEL_API kstd::chrono::milliseconds KE_TIME_GetSystemTime() noexcept {
  return {};
}
