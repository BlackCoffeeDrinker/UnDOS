
#pragma once

#include "Core.hpp"

namespace kernel::vmm {
// Initializes the raw hardware paging structures (e.g., sets up recursive mapping)
void init(const BootInfoT &boot_info) noexcept;
void late_init() noexcept;

void *allocate_user_memory(AddressSpace &as, size_t size, ProtectFlags flags) noexcept;
}// namespace kernel::vmm
