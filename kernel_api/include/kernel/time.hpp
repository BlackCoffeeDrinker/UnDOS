
#pragma once
#include <kernel/__core.hpp>

#include <chrono.hpp>

namespace kernel::time {
UNDOS_KERNEL_API void se_set_time_increment(uint32_t nanoseconds) noexcept;
UNDOS_KERNEL_API void ke_update_system_time() noexcept;
UNDOS_KERNEL_API [[nodiscard]] uint64_t ke_query_system_time_ms() noexcept;

}// namespace kernel::time
