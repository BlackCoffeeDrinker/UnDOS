
#pragma once
#include "Slab.hpp"
#include <Kernel.hpp>
#include <kernel/adt/avl_tree.hpp>

namespace kernel::memory {
class Cache;

void set_heap_start(VirtualAddress start) noexcept;
Slab *find_slab(void *ptr) noexcept;

class Allocator {
  Layout layout_;
  size_t total_allocated_ = 0;

  public:
  explicit Allocator(const Layout &layout) noexcept : layout_(layout) {}

  Slab *allocate_slab(Cache *cache) noexcept;
  void free_slab(Slab *slab) noexcept;

  size_t total_allocated_bytes() const noexcept { return total_allocated_; }
};

}// namespace kernel::memory
