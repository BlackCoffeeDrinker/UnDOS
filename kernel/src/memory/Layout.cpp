
#include "Layout.hpp"
#include "Slab.hpp"
#include "Storage.hpp"

namespace kernel::memory {

Layout calculate_layout(size_t object_size, size_t alignment, size_t page_size) noexcept {
  Layout layout{};
  layout.page_size = page_size;
  layout.object_size = (object_size + alignment - 1) & ~(alignment - 1);

  // For now, we always use a single page.
  // Larger objects might need multiple pages, but we'll stick to 1 page for now.
  layout.slab_size = page_size;

  // We need: N * object_size + sizeof(Slab) + N * sizeof(Storage) <= slab_size
  // N * (object_size + sizeof(Storage)) <= slab_size - sizeof(Slab)

  if (layout.slab_size <= sizeof(Slab)) {
    layout.objects = 0;
    return layout;
  }

  size_t available = layout.slab_size - sizeof(Slab);
  layout.objects = available / (layout.object_size + sizeof(Storage));

  layout.object_offset = 0;
  // Align control_offset to 8 bytes
  layout.control_offset = (layout.objects * layout.object_size + 7) & ~static_cast<size_t>(7);

  // Adjust if it overflows due to alignment
  while (layout.objects > 0 &&
         (layout.control_offset + sizeof(Slab) + layout.objects * sizeof(Storage) > layout.slab_size)) {
    layout.objects--;
    layout.control_offset = (layout.objects * layout.object_size + 7) & ~static_cast<size_t>(7);
  }

  return layout;
}

}// namespace kernel::memory
