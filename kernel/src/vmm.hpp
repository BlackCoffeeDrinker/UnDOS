
#pragma once

#include "Core.hpp"

namespace hal::vmm {
// Initializes the raw hardware paging structures (e.g., sets up recursive mapping)
void init() noexcept;

// Low-level hardware mechanics: write the mapping to the actual CPU tables
bool lower_map_page(uintptr_t virtual_addr, uintptr_t physical_addr, ProtectFlags flags) noexcept;

// Low-level hardware mechanics: clear the mapping from the actual CPU tables
void lower_unmap_page(uintptr_t virtual_addr) noexcept;

// Force the hardware to flush its address translation caches (TLB)
void flush_tlb(uintptr_t virtual_addr) noexcept;
}// namespace hal::vmm
