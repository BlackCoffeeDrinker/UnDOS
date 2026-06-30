
#pragma once
#include <stddef.hpp>

namespace kernel::memory {
struct Layout {
  size_t object_size;   // Actual size of each object
  size_t object_offset; // Start of objects in the slab
  size_t objects;       // Number of objects in the slab
  size_t control_offset;// Start of metadata (Slab + Storage nodes)
  size_t slab_size;     // Total size (usually 1 or more pages)
  size_t page_size;     // Page size from boot info
};

Layout calculate_layout(size_t object_size, size_t alignment, size_t page_size) noexcept;

}// namespace kernel::memory
