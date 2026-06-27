
#pragma once

#include "structs.hpp"
#include <kernel/boot/boot_info.hpp>

namespace hal::x86 {
void init_pmm(const kernel::BootInfoT &boot_info) noexcept;
}
