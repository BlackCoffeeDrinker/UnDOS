#include "slab.hpp"
#include "../vmm.hpp"
#include <kernel/hal_interface.hpp>

namespace kernel::memory {

static VirtualAddress kernel_heap_ceiling = 0xE0000000;

// --- KmemCache Implementation ---

KmemCache::KmemCache(size_t object_size, size_t alignment) noexcept
    : m_object_size(object_size < sizeof(void *) ? sizeof(void *) : object_size),
      m_alignment(alignment) {}

void KmemCache::list_add(Slab **head, Slab *s) noexcept {
  s->next = *head;
  s->prev = nullptr;
  if (*head) (*head)->prev = s;
  *head = s;
}

void KmemCache::list_remove(Slab **head, Slab *s) noexcept {
  if (s->prev) s->prev->next = s->next;
  else
    *head = s->next;
  if (s->next) s->next->prev = s->prev;
}

void KmemCache::init_slab_on_page(Slab *s, void *page) noexcept {
  s->cache = this;
  s->next = s->prev = nullptr;

  constexpr size_t header_size = sizeof(Slab);
  const size_t align = m_alignment > 0 ? m_alignment : 8;
  const size_t offset = (header_size + align - 1) & ~(align - 1);
  const size_t page_size = SlabAllocator::instance().get_page_size();

  s->first_free = reinterpret_cast<void *>(static_cast<uintptr_t>(VirtualAddress::from_ptr(page) + offset));
  s->total_count = (page_size - offset) / m_object_size;
  s->free_count = s->total_count;

  // Link all objects in the slab
  auto current = reinterpret_cast<uintptr_t>(s->first_free);
  for (uint32_t i = 0; i < s->total_count - 1; ++i) {
    *reinterpret_cast<uintptr_t *>(current) = current + m_object_size;
    current += m_object_size;
  }
  *reinterpret_cast<uintptr_t *>(current) = 0;
}

void *KmemCache::alloc() noexcept {
  Slab *s = m_slabs_partial;
  if (!s) {
    s = m_slabs_free;
    if (!s) return nullptr;

    list_remove(&m_slabs_free, s);
    list_add(&m_slabs_partial, s);
  }

  void *obj = s->first_free;
  s->first_free = reinterpret_cast<void *>(*static_cast<uintptr_t *>(obj));
  s->free_count--;

  if (s->free_count == 0) {
    list_remove(&m_slabs_partial, s);
    list_add(&m_slabs_full, s);
  }

  return obj;
}

void KmemCache::free(void *ptr) noexcept {
  const size_t page_size = SlabAllocator::instance().get_page_size();
  const auto s = VirtualAddress::from_ptr(ptr).align_down(page_size).as_ptr<Slab>();

  *static_cast<uintptr_t *>(ptr) = reinterpret_cast<uintptr_t>(s->first_free);
  s->first_free = ptr;
  s->free_count++;

  if (s->free_count == 1) {
    list_remove(&m_slabs_full, s);
    list_add(&m_slabs_partial, s);
  } else if (s->free_count == s->total_count) {
    list_remove(&m_slabs_partial, s);
    list_add(&m_slabs_free, s);
  }
}

// --- SlabAllocator Implementation ---

SlabAllocator &SlabAllocator::instance() noexcept {
  static SlabAllocator inst;
  return inst;
}

void SlabAllocator::init(size_t page_size) noexcept {
  m_page_size = page_size;
  static const size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};

  for (int i = 0; i < 9; ++i) {
    m_malloc_caches_obj[i] = KmemCache(sizes[i], sizes[i]);
    m_malloc_caches[i] = &m_malloc_caches_obj[i];
  }
}

void *SlabAllocator::alloc_pages(size_t count) noexcept {
  const PhysicalAddress phys = HAL_PMM_Allocate_Frames(count);
  if (!phys) return nullptr;

  const VirtualAddress virt = kernel_heap_ceiling;
  kernel_heap_ceiling += count * m_page_size;

  for (size_t i = 0; i < count; ++i) {
    if (!HAL_VMM_MapPage(virt + i * m_page_size, phys + i * m_page_size,
                         kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE)) {
      return nullptr;
    }
    HAL_VMM_Flush(virt + i * m_page_size);
  }

  return virt.as_ptr();
}

void *SlabAllocator::kmalloc(size_t size) noexcept {
  for (int i = 0; i < 9; ++i) {
    if (size <= m_malloc_caches[i]->object_size()) {
      void *ptr = m_malloc_caches[i]->alloc();
      if (ptr) return ptr;

      // Need a new page
      void *page = alloc_pages(1);
      if (!page) return nullptr;

      auto *s = reinterpret_cast<KmemCache::Slab *>(page);
      m_malloc_caches[i]->init_slab_on_page(s, page);
      m_malloc_caches[i]->list_add(&m_malloc_caches[i]->m_slabs_partial, s);

      return m_malloc_caches[i]->alloc();
    }
  }

  const size_t pages = (size + m_page_size - 1) / m_page_size;
  void *ptr = alloc_pages(pages);
  if (ptr) {
    static_cast<KmemCache::Slab *>(ptr)->cache = nullptr;
  }
  return ptr;
}

void SlabAllocator::kfree(void *ptr) noexcept {
  if (!ptr) return;
  const auto s = VirtualAddress::from_ptr(ptr).align_down(m_page_size).as_ptr<KmemCache::Slab>();
  if (s->cache) {
    s->cache->free(ptr);
  }
  // Note: currently we don't free large page-aligned allocations.
  // This is consistent with previous implementation.
}

void *SlabAllocator::allocate_user_memory(kernel::vmm::AddressSpace &as, size_t size, kernel::vmm::ProtectFlags flags) noexcept {
  const size_t count = (size + m_page_size - 1) / m_page_size;

  const PhysicalAddress phys = HAL_PMM_Allocate_Frames(count);
  if (!phys) return nullptr;

  // Use the AddressSpace's VAD tree to find a free region
  void *virt = as.allocate_region(size, flags);
  if (!virt) {
    HAL_PMM_Free_Frames(phys, count);
    return nullptr;
  }

  VirtualAddress v_addr = VirtualAddress::from_ptr(virt);
  for (size_t i = 0; i < count; ++i) {
    if (!HAL_VMM_MapPage(v_addr + i * m_page_size, phys + i * m_page_size, flags | kernel::vmm::ProtectFlags::USER)) {
      return nullptr;
    }
    HAL_VMM_Flush(v_addr + i * m_page_size);
  }

  return virt;
}

// Global Wrappers
void slab_init(size_t page_size) noexcept {
  SlabAllocator::instance().init(page_size);
}

void *kmalloc(size_t size) noexcept {
  return SlabAllocator::instance().kmalloc(size);
}

void kfree(void *ptr) noexcept {
  SlabAllocator::instance().kfree(ptr);
}

} // namespace kernel::memory
