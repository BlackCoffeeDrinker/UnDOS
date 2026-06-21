#pragma once

#include <kernel/__core.hpp>

#include "entry.hpp"

#include "memory/physical_memory.hpp"
#include "memory/virtual_memory.hpp"

#include "arch/early_debug.hpp"
#include "arch/virtual_memory.hpp"


UNDOS_HAL_API void HAL_PlatformInit(kernel::boot_info_t &boot_info);
