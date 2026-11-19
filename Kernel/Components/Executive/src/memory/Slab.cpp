
#include "Slab.hpp"
#include "Cache.hpp"
#include <new.hpp>

namespace kernel::memory {

Slab::Slab(const Layout &layout, kstd::span<uint8_t> memory, Cache *cache) noexcept
    : layout_objects_(layout.objects), cache_(cache), memory_(memory) {

  uint8_t *control_ptr = memory.data() + layout.control_offset;
  storage_base_ = reinterpret_cast<Storage *>(control_ptr + sizeof(Slab));

  uint8_t *object_ptr = memory.data() + layout.object_offset;

  for (size_t i = 0; i < layout.objects; ++i) {
    auto *node = new (&storage_base_[i]) Storage(object_ptr + i * layout.object_size);
    freelist_.push_back(node);
  }
}

void *Slab::allocate() noexcept {
  if (freelist_.empty()) return nullptr;

  Storage *node = &freelist_.front();
  freelist_.remove(node);
  allocated_++;
  return node->pointer;
}

void Slab::free(void *ptr) noexcept {
  const uintptr_t offset = reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(memory_.data());

  const size_t index = offset / cache_->object_size();
  Storage *node = &storage_base_[index];

  freelist_.push_back(node);
  allocated_--;
}

}// namespace kernel::memory
