
#pragma once
#include "Slab.hpp"
#include <Kernel.hpp>
#include <kernel/adt/avl_tree.hpp>
#include "ArenaMutex.hpp"

namespace kernel::memory {
class Cache;

Slab *find_slab(void *ptr) noexcept;
// Serializes the kernel virtual arena. Never acquire while holding a cache
// or registry spinlock, or from hardware interrupt context.
ArenaMutex &arena_mutex() noexcept;

// Fallback allocator for objects that don't fit any fixed-size Cache. Backed
// directly by page frames pulled from the same bump-allocated kernel heap
// range used by the slab Allocator, with the block size stashed in a small
// header just before the returned pointer.
void *allocate_large(size_t size) noexcept;
void free_large(void *ptr) noexcept;

class Allocator {
  Layout layout_;
  size_t total_allocated_ = 0;

  public:
  explicit Allocator(const Layout &layout) noexcept : layout_(layout) {}

  Slab *allocate_slab(Cache &cache) noexcept;
  void free_slab(Slab &slab) noexcept;

  size_t total_allocated_bytes() const noexcept { return total_allocated_; }
};

}// namespace kernel::memory
