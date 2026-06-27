
#include "vmm.hpp"

namespace {
// Define a safe, isolated 256MB window in the High-Half for early bootstrap allocations
constexpr uintptr_t EARLY_HEAP_START = 0xE0000000;
constexpr uintptr_t EARLY_HEAP_LIMIT = 0xF0000000;

uintptr_t page_size;

// Track the active state of our primitive allocator memory bounds
uintptr_t g_early_heap_bump = EARLY_HEAP_START;
uintptr_t g_mapped_ceiling = EARLY_HEAP_START;
}// namespace

namespace kernel::vmm {
void init(const BootInfoT &boot_info) noexcept {
  page_size = boot_info.page_size;

  g_early_heap_bump = 0;
  for (auto i : boot_info.memory_map) {
    if (i.type == MemoryRegionType::None) {
      continue;
    }

    if (i.type != MemoryRegionType::Available) {
      continue;
    }

    if (g_early_heap_bump < i.base) {
      g_early_heap_bump = i.base;
      g_mapped_ceiling = static_cast<uintptr_t>(i.base + i.length);
    }
  }
}

[[nodiscard]] void *early_alloc(size_t size) noexcept {
  if (size == 0) [[unlikely]] {
    return nullptr;
  }

  // 1. Enforce strict 8-byte alignment compliance across allocations
  size = (size + 7) & ~static_cast<size_t>(7);

  const uintptr_t target_allocation = g_early_heap_bump;
  const uintptr_t expected_next_bump = g_early_heap_bump + size;

  // 2. Prevent the early heap from eating into other reserved system memory spaces
  if (expected_next_bump >= EARLY_HEAP_LIMIT) [[unlikely]] {
    return nullptr;// Out of early virtual address pool capacity
  }

  // 3. Demand Paging Loop: If our allocation footprint pushes past the
  // current mapped physical boundary, provision new pages on-demand.
  while (expected_next_bump > g_mapped_ceiling) {
    // Acquire a physical page frame directly via the PMM implementation
    const uintptr_t physical_frame = HAL_PMM_Allocate_Frames(1);
    if (!physical_frame) {
      return nullptr;// Out of physical system memory!
    }

    // Map the new physical frame to our current virtual execution ceiling
    using vmm::ProtectFlags;
    const bool map_success = HAL_VMM_MapPage(
        g_mapped_ceiling,
        physical_frame,
        ProtectFlags::READ | ProtectFlags::WRITE);

    if (!map_success) {
      return nullptr;// Page table exhaustion or allocation collision
    }

    // Clear stale cached translations for the newly allocated virtual memory block
    HAL_VMM_Flush(g_mapped_ceiling);

    // Expand our mapped footprint threshold safely forward
    g_mapped_ceiling += page_size;
  }

  // Commit the bump allocator update and hand back the pointer
  g_early_heap_bump = expected_next_bump;
  return reinterpret_cast<void *>(target_allocation);
}

void early_free(void *ptr) noexcept {
  if (!ptr) [[unlikely]] {
    return;
  }
}

}// namespace kernel::vmm
