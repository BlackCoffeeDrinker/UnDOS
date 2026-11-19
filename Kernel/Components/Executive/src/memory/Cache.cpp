
#include "Cache.hpp"

namespace kernel::memory {

Cache::Cache(size_t object_size, size_t alignment, size_t page_size) noexcept
    : layout_(calculate_layout(object_size, alignment, page_size)), allocator_(layout_) {
}

void *Cache::allocate() noexcept {
  {
    IrqSpinLockGuard guard(lock_);
    if (void *ptr = allocate_locked()) return ptr;
  }
  // Slab acquisition can map pages and wait for the arena. Never nest it
  // inside the cache's IRQ-masked metadata transaction.
  Slab *slab = allocator_.allocate_slab(*this);
  if (!slab) return nullptr;
  IrqSpinLockGuard guard(lock_);
  free_slabs_.push_back(slab);
  return allocate_locked();
}

void *Cache::allocate_locked() noexcept {
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
  IrqSpinLockGuard guard(lock_);
  Slab *slab = find_slab(ptr);
  if (slab) free_locked(ptr, *slab);
}

void Cache::free(void *ptr, Slab &slab) noexcept {
  IrqSpinLockGuard guard(lock_);
  free_locked(ptr, slab);
}

void Cache::free_locked(void *ptr, Slab &slab) noexcept {
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

  return nullptr;
}

size_t Cache::reclaim() noexcept {
  common::DoubleList<Slab> detached;
  {
    IrqSpinLockGuard guard(lock_);
    while (!free_slabs_.empty()) {
      Slab *slab = &free_slabs_.front();
      free_slabs_.remove(slab);
      detached.push_back(slab);
    }
  }
  size_t count = 0;
  while (!detached.empty()) {
    Slab *slab = &detached.front();
    detached.remove(slab);
    allocator_.free_slab(*slab);
    count++;
  }
  return count;
}

}// namespace kernel::memory
