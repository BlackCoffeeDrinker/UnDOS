
#pragma once
#include <kernel/__core.hpp>

namespace kernel::pmm {
// Allocate a single 4KB physical page frame. Returns 0 on out-of-memory.
UNDOS_KERNEL_API uintptr_t allocate_frame();

// Free a previously allocated physical page frame
UNDOS_KERNEL_API void free_frame(uintptr_t physical_address);

// Mark a specific region as reserved (used for kernel, modules, etc.)
UNDOS_KERNEL_API void reserve_region(uintptr_t base, size_t length);
}// namespace kernel::pmm
