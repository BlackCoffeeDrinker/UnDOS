
#include "Cache.hpp"

namespace kernel::memory {

Cache::Cache(size_t object_size, size_t alignment, size_t page_size) noexcept
    : layout_(calculate_layout(object_size, alignment, page_size)), allocator_(layout_) {
}

void *Cache::allocate() noexcept {
  Slab *slab = get_available_slab();
  if (!slab) return nullptr;

  const bool was_empty = slab->empty();
  void *ptr = slab->allocate();
  allocated_ += layout_.object_size;

  if (was_empty) {
    // Slab was sitting in free_slabs_ (either freshly allocated or fully
    // freed and reused). It may go straight to full when only a single
    // object fits per slab, so it must be removed from free_slabs_ either
    // way, not assumed to land in partial_slabs_ first.
    free_slabs_.remove(slab);
    if (slab->full()) {
      full_slabs_.push_back(slab);
    } else {
      partial_slabs_.push_back(slab);
    }
  } else if (slab->full()) {
    partial_slabs_.remove(slab);
    full_slabs_.push_back(slab);
  }

  return ptr;
}

void Cache::free(void *ptr) noexcept {
  Slab *slab = find_slab(ptr);
  if (!slab) return;
  free(ptr, *slab);
}

void Cache::free(void *ptr, Slab &slab) noexcept {
  const bool was_full = slab.full();
  slab.free(ptr);
  allocated_ -= layout_.object_size;

  if (slab.empty()) {
    if (!was_full) {
      partial_slabs_.remove(&slab);
    } else {
      full_slabs_.remove(&slab);
    }
    free_slabs_.push_back(&slab);
  } else if (was_full) {
    full_slabs_.remove(&slab);
    partial_slabs_.push_back(&slab);
  }
}

Slab *Cache::get_available_slab() noexcept {
  if (!partial_slabs_.empty()) return &partial_slabs_.front();
  if (!free_slabs_.empty()) return &free_slabs_.front();

  Slab *new_slab = allocator_.allocate_slab(*this);
  if (new_slab) {
    free_slabs_.push_back(new_slab);
  }
  return new_slab;
}

size_t Cache::reclaim() noexcept {
  size_t count = 0;
  while (!free_slabs_.empty()) {
    Slab *slab = &free_slabs_.front();
    free_slabs_.remove(slab);
    allocator_.free_slab(*slab);
    count++;
  }
  return count;
}

}// namespace kernel::memory
