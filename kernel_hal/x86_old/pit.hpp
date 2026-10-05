
#pragma once
#include "structs.hpp"

namespace hal::x86 {
void set_system_timer_frequency(uint32_t target_hz) noexcept;
}
