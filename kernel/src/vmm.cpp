
#include "vmm.hpp"

namespace kernel::vmm {

void init() noexcept {
  // Call the HAL to set up the physical layout (like the recursive entry)
  arch::vmm::init();
}

bool map_page(uintptr_t virtual_addr, uintptr_t physical_addr, arch::vmm::ProtectFlags flags) {
  // 1. High-level policy checks happen here!
  // Example: Is this virtual address inside a protected kernel zone?
  // Example: Check our internal process allocation structures to ensure no overlap.

  // 2. Delegate the raw hardware bit-twiddling entirely to the HAL
  bool success = arch::vmm::lower_map_page(virtual_addr, physical_addr, flags);

  if (success) {
    arch::vmm::flush_tlb(virtual_addr);
  }

  return success;
}

}// namespace kernel::vmm
