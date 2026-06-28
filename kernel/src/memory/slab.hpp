#pragma once

#include <stddef.h>
#include <stdint.h>
#include <kernel/memory/virtual_memory.hpp>

namespace kernel::vmm {
struct AddressSpace;
}

namespace kernel::memory {

class KmemCache {
  friend class SlabAllocator;
public:
  KmemCache() = default;
  KmemCache(size_t object_size, size_t alignment) noexcept;

  void *alloc() noexcept;
  void free(void *ptr) noexcept;

  size_t object_size() const noexcept { return m_object_size; }

private:
  struct Slab {
    Slab *next;
    Slab *prev;
    void *first_free;
    uint32_t free_count;
    uint32_t total_count;
    KmemCache *cache;
  };

  size_t m_object_size{0};
  size_t m_alignment{0};

  Slab *m_slabs_full{nullptr};
  Slab *m_slabs_partial{nullptr};
  Slab *m_slabs_free{nullptr};

  void list_add(Slab **head, Slab *s) noexcept;
  void list_remove(Slab **head, Slab *s) noexcept;
  void init_slab_on_page(Slab *s, void *page) noexcept;
};

class SlabAllocator {
public:
  static SlabAllocator &instance() noexcept;

  void init(size_t page_size) noexcept;

  void *kmalloc(size_t size) noexcept;
  void kfree(void *ptr) noexcept;

  // User-space allocation support
  void *allocate_user_memory(kernel::vmm::AddressSpace &as, size_t size, kernel::vmm::ProtectFlags flags) noexcept;

  size_t get_page_size() const noexcept { return m_page_size; }

private:
  SlabAllocator() = default;

  size_t m_page_size{4096};
  KmemCache *m_malloc_caches[9]{nullptr};
  KmemCache m_malloc_caches_obj[9];

  void *alloc_pages(size_t count) noexcept;
};

void slab_init(size_t page_size) noexcept;
void *kmalloc(size_t size) noexcept;
void kfree(void *ptr) noexcept;

} // namespace kernel::memory
