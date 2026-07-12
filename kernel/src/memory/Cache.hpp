
#pragma once
#include <kernel/common/DoubleList.hpp>
#include "Allocator.hpp"
#include "Layout.hpp"
#include "Slab.hpp"

namespace kernel::memory {

class Cache {
  Layout layout_;
  Allocator allocator_;

  common::DoubleList<Slab> free_slabs_;
  common::DoubleList<Slab> partial_slabs_;
  common::DoubleList<Slab> full_slabs_;

  size_t allocated_ = 0;

  Slab *get_available_slab() noexcept;

  public:
  Cache() noexcept : layout_{}, allocator_{layout_} {}
  Cache(size_t object_size, size_t alignment, size_t page_size) noexcept;

  void *allocate() noexcept;
  void free(void *ptr) noexcept;
  void free(void *ptr, Slab &slab) noexcept;

  size_t reclaim() noexcept;

  [[nodiscard]] size_t object_size() const noexcept { return layout_.object_size; }
  [[nodiscard]] size_t allocated_bytes() const noexcept { return allocated_; }
};

}// namespace kernel::memory
