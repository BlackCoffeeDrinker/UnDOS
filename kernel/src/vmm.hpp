
#pragma once

#include "stdkrn.hpp"

namespace kernel::vmm {
// Initializes the raw hardware paging structures (e.g., sets up recursive mapping)
void init() noexcept;
void late_init() noexcept;

void *allocate_user_memory(AddressSpace &as, size_t size, ProtectFlags flags) noexcept;

// Creates a fully-initialized, isolated user address space backed by a fresh
// HAL translation root (kernel higher-half cloned, user half empty).
// Returns false if the underlying HAL allocation failed.
bool create_user_address_space(AddressSpace &out_as) noexcept;

// Reclaims a user address space previously built by create_user_address_space:
// frees any physical frames still tracked by its VADs, then destroys the
// underlying HAL translation root.
void destroy_user_address_space(AddressSpace &as) noexcept;
}// namespace kernel::vmm
