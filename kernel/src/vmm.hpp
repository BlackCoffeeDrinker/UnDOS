
#pragma once

#include "Core.hpp"

namespace kernel::vmm {
// Initializes the raw hardware paging structures (e.g., sets up recursive mapping)
void init(const BootInfoT &boot_info) noexcept;
[[nodiscard]] void *early_alloc(size_t size) noexcept;
void early_free(void *ptr) noexcept;

template<typename T>
struct early_deleter {
  void operator()(T *ptr) const noexcept {
    if (ptr) { early_free(ptr); }
  }
};

}// namespace kernel::vmm
