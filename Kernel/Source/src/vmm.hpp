
#pragma once

#include "stdkrn.hpp"
#include <kernel/kerror.hpp>
#include <expected.hpp>

namespace kernel::vmm {
// Initializes the raw hardware paging structures (e.g., sets up recursive mapping)
void init() noexcept;
void late_init() noexcept;

 kstd::expected<void *, kstd::error_code> allocate_memory(AddressSpace &as, size_t size, ProtectFlags flags) noexcept;
kstd::error_code map_fixed_memory(AddressSpace &as, uintptr_t address, size_t size, ProtectFlags flags) noexcept;

// Creates a fully-initialized, isolated user address space backed by a fresh
// HAL translation root (kernel higher-half cloned, user half empty).
// Returns false if the underlying HAL allocation failed.
kstd::error_code create_user_address_space(AddressSpace &out_as) noexcept;

// Reclaims a user address space previously built by create_user_address_space:
// frees any physical frames still tracked by its VADs, then destroys the
// underlying HAL translation root.
void destroy_user_address_space(AddressSpace &as) noexcept;

// Test/diagnostic helper: returns the number of bytes currently outstanding
// (allocated but not yet freed) in the fixed-size KE_Malloc/KE_Free slab
// cache that would service a `KE_Malloc(size)` request, i.e. the same cache
// selection logic used by KE_Malloc itself. Used by host tests to prove that
// object creation helpers (KE_CreateObject, KE_CreateKPtrObject,
// kernel::CreateKObject) return their memory to the allocator instead of
// leaking it.
size_t debug_slab_cache_allocated_bytes(size_t size) noexcept;
}// namespace kernel::vmm
