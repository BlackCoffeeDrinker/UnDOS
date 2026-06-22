
#include "vmm.hpp"

#include <tuple.hpp>

namespace hal::vmm {

void init() noexcept {
  // Call the HAL to set up the physical layout (like the recursive entry)
}

bool map_page(uintptr_t virtual_addr, uintptr_t physical_addr, ProtectFlags flags) {
  kstd::ignore = virtual_addr;
  kstd::ignore = physical_addr;
  kstd::ignore = flags;
  
  // 1. High-level policy checks happen here!
  return false;
}

}// namespace hal::vmm
