
#pragma once

#include <kernel/__core.hpp>
#include <kernel/boot/boot_info.hpp>

UNDOS_KERNEL_API [[noreturn]] void kernel_core_main(const kernel::boot_info_t &boot_info);
UNDOS_HAL_API void HAL_PlatformInit(const kernel::boot_info_t &boot_info);
