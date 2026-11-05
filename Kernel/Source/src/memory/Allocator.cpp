
#include "Allocator.hpp"
#include "Cache.hpp"
#include "../vmm.hpp"
#include <kernel/hal_interface.hpp>
#include <kernel/spinlock.hpp>
#include <new.hpp>

extern uint32_t g_page_size;

namespace kernel::memory {
#if __STDC_HOSTED__
namespace test_hooks {
void (*slowPages)(bool) = nullptr;
}
#endif
namespace {
adt::AvlTree<Slab, &Slab::avl_node> g_global_registry;
SpinLock g_registry_lock;

constexpr uint32_t kLargeAllocMagic = 0x4C524745;// 'LRGE'

struct LargeAllocHeader {
  uint32_t magic;
  size_t pages;
};

ArenaMutex g_arena_mutex;

void *allocate_pages(size_t bytes) noexcept {
#if __STDC_HOSTED__
  if (test_hooks::slowPages) test_hooks::slowPages(false);
#endif
  auto &as = KE_VMM_GetKernelAddressSpace();
  if (!g_page_size || !bytes || bytes > SIZE_MAX - (g_page_size - 1)) return nullptr;
  const size_t pages = (bytes + g_page_size - 1) / g_page_size;
  const size_t mapped_bytes = pages * g_page_size;
  if (as.current_base > as.limit ||
      g_page_size > as.limit.value - as.current_base.value ||
      mapped_bytes > as.limit.value - as.current_base.value - g_page_size)
    return nullptr;
  const auto physical = HAL_PMM_AllocateFrames(pages);
  if (!physical) return nullptr;
  const VirtualAddress address = as.current_base + g_page_size;
  const auto original_base = as.current_base;
  as.current_base = address + mapped_bytes;
  size_t mapped = 0;
  for (; mapped < pages; ++mapped) {
    const auto page = address + mapped * g_page_size;
    if (!HAL_VMM_MapPage(page, physical + mapped * g_page_size,
                        vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE))
      break;
    HAL_SMP_ShootdownTlb(page);
    __builtin_memset(page.as_ptr(), 0, g_page_size);
  }
  if (mapped != pages) {
    for (size_t i = 0; i < mapped; ++i) {
      const auto page = address + i * g_page_size;
      HAL_VMM_UnmapPage(page);
      HAL_SMP_ShootdownTlb(page);
    }
    HAL_PMM_FreeFrames(physical, pages);
    as.current_base = original_base;
    return nullptr;
  }
  return address.as_ptr();
}

void free_pages(VirtualAddress address, size_t pages) noexcept {
#if __STDC_HOSTED__
  if (test_hooks::slowPages) test_hooks::slowPages(true);
#endif
  const auto physical = HAL_VMM_GetPhysicalAddress(address);
  for (size_t i = 0; i < pages; ++i) {
    const auto page = address + i * g_page_size;
    HAL_VMM_UnmapPage(page);
    HAL_SMP_ShootdownTlb(page);
  }
  HAL_PMM_FreeFrames(physical, pages);
}
}// namespace

ArenaMutex &arena_mutex() noexcept { return g_arena_mutex; }

Slab *find_slab(void *ptr) noexcept {
  IrqSpinLockGuard guard(g_registry_lock);
  Slab *slab = g_global_registry.find(reinterpret_cast<uintptr_t>(ptr));
  return slab;
}

Slab *Allocator::allocate_slab(Cache &cache) noexcept {
  kstd::unique_lock arena(arena_mutex());
  void *allocation = allocate_pages(layout_.slab_size);
  if (!allocation) return nullptr;

  const kstd::span memory(static_cast<uint8_t *>(allocation), layout_.slab_size);

  void *slab_storage = memory.data() + layout_.control_offset;
  auto *slab = new (slab_storage) Slab(layout_, memory, &cache);

  {
    IrqSpinLockGuard guard(g_registry_lock);
    g_global_registry.insert(*slab);
  }
  total_allocated_ += layout_.slab_size;

  return slab;
}

void *allocate_large(size_t size) noexcept {
  if (size > SIZE_MAX - sizeof(LargeAllocHeader)) return nullptr;
  const size_t total = size + sizeof(LargeAllocHeader);
  const size_t page_count = total / g_page_size + (total % g_page_size != 0);

  kstd::unique_lock arena(arena_mutex());
  // Heap pages are eager and explicitly freed; no VAD allocation may recurse
  // into a cache while the arena is held.
  void *virt_ptr = allocate_pages(total);
  if (!virt_ptr) return nullptr;

  auto *header = static_cast<LargeAllocHeader *>(virt_ptr);
  header->magic = kLargeAllocMagic;
  header->pages = page_count;

  return static_cast<uint8_t *>(virt_ptr) + sizeof(LargeAllocHeader);
}

void free_large(void *ptr) noexcept {
  const VirtualAddress virt = VirtualAddress::from_ptr(ptr) - sizeof(LargeAllocHeader);
  const auto *header = virt.as_ptr<LargeAllocHeader>();

  kstd::unique_lock arena(arena_mutex());

  if (header->magic != kLargeAllocMagic) [[unlikely]] {
    KE_Print("free_large: invalid header at {}", ptr);
    return;
  }

  const size_t pages = header->pages;
  free_pages(virt, pages);
}

void Allocator::free_slab(Slab &slab) noexcept {
  kstd::unique_lock arena(arena_mutex());
  {
    IrqSpinLockGuard guard(g_registry_lock);
    g_global_registry.remove(&slab);
  }

  total_allocated_ -= layout_.slab_size;

  const VirtualAddress virt = VirtualAddress::from_ptr(slab.memory().data());

  const size_t pages = slab.memory().size() / layout_.page_size;
  free_pages(virt, pages);
}


}// namespace kernel::memory
