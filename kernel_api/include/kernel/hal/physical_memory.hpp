#pragma once
#include <kernel/__core.hpp>

// Low-level mechanism: Allocate a single physical frame
UNDOS_HAL_API uintptr_t PMM_Allocate_Frames(size_t count) noexcept;

// Low-level mechanism: Free a physical frame
UNDOS_HAL_API void PMM_Free_Frame(uintptr_t physical_address) noexcept;

// Low-level mechanism: Reserve a region of physical memory
UNDOS_HAL_API void PMM_Reserve_Region(uintptr_t base, size_t length) noexcept;
