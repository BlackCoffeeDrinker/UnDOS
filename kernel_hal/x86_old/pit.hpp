
#pragma once
#include <stdint.h>

#include "structs.hpp"

namespace hal::x86 {
void configure_frequency(uint32_t target_hz) noexcept;
}
