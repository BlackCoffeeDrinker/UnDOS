
#pragma once
#include <kernel/__core.hpp>

#include <chrono.hpp>

UNDOS_KERNEL_API void KE_TIME_SetIncrement(kstd::chrono::nanoseconds nanoseconds) noexcept;
UNDOS_KERNEL_API void KE_TIME_UpdateSystemTime() noexcept;
UNDOS_KERNEL_API [[nodiscard]] kstd::chrono::milliseconds KE_TIME_GetSystemTime() noexcept;
