
#pragma once
#include <kernel/__core.hpp>

#include <chrono.hpp>

UNDOS_KERNEL_API void se_set_time_increment(kstd::chrono::nanoseconds nanoseconds) noexcept;
UNDOS_KERNEL_API void ke_update_system_time() noexcept;
UNDOS_KERNEL_API [[nodiscard]] kstd::chrono::milliseconds ke_query_system_time_ms() noexcept;
