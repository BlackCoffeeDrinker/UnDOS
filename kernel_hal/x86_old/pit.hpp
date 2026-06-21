
#pragma once
#include <stdint.h>

#include "structs.hpp"

namespace kernel::x86::pit {
void configure_frequency(uint32_t target_hz) noexcept;
}
