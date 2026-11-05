
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "structs.hpp"
#include <kernel/boot/boot_info.hpp>

namespace hal::x86 {
void init_pmm(const kernel::BootInfoT &boot_info) noexcept;
void report_pmm(const kernel::BootInfoT &boot_info) noexcept;
bool assign_frame_owner(kernel::PhysicalAddress frame, uint32_t pid) noexcept;
bool free_owned_frames(uint32_t pid, kernel::PhysicalAddress base, size_t count) noexcept;
}
