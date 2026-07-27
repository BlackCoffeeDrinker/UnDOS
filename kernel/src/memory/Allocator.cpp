
#include "Allocator.hpp"
#include "Cache.hpp"
#include "../vmm.hpp"
#include <kernel/hal_interface.hpp>
#include <kernel/spinlock.hpp>
#include <new.hpp>

extern uint32_t g_page_size;

namespace kernel::memory {
namespace {
adt::AvlTree<Slab, &Slab::avl_node> g_global_registry;
SpinLock g_registry_lock;

constexpr uint32_t kLargeAllocMagic = 0x4C524745;// 'LRGE'

struct LargeAllocHeader {
  uint32_t magic;
  size_t pages;
};

SpinLock g_large_alloc_lock;
}// namespace


Slab *find_slab(void *ptr) noexcept {
  g_registry_lock.lock();
  Slab *slab = g_global_registry.find(reinterpret_cast<uintptr_t>(ptr));
  g_registry_lock.unlock();
  return slab;
}

Slab *Allocator::allocate_slab(Cache &cache) noexcept {
  const size_t pages = layout_.slab_size / layout_.page_size;
  const PhysicalAddress phys = HAL_PMM_AllocateFrames(pages);
  if (!phys) {
    early_print_fmt("Failed to allocate physical frames for slab!\n\r");
    return nullptr;
  }

  auto &as = KE_VMM_GetKernelAddressSpace();
  const VirtualAddress virt = as.current_base + layout_.page_size;
  as.current_base = virt + layout_.slab_size;

  for (size_t i = 0; i < pages; ++i) {
    HAL_VMM_MapPage(
        virt + i * layout_.page_size,
        phys + i * layout_.page_size,
        vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE);
  }

  const kstd::span memory(virt.as_ptr<uint8_t>(), layout_.slab_size);

  void *slab_storage = memory.data() + layout_.control_offset;
  auto *slab = new (slab_storage) Slab(layout_, memory, &cache);

  g_registry_lock.lock();
  g_global_registry.insert(*slab);
  g_registry_lock.unlock();
  total_allocated_ += layout_.slab_size;

  return slab;
}

void *allocate_large(size_t size) noexcept {
  const size_t total = size + sizeof(LargeAllocHeader);

  g_large_alloc_lock.lock();

  void *virt_ptr = vmm::allocate_memory(
      KE_VMM_GetKernelAddressSpace(), total,
      vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE);

  if (!virt_ptr) {
    g_large_alloc_lock.unlock();
    early_print_fmt("Failed to allocate large allocation!\n\r");
    return nullptr;
  }

  auto *header = static_cast<LargeAllocHeader *>(virt_ptr);
  header->magic = kLargeAllocMagic;
  header->pages = (total + g_page_size - 1) / g_page_size;

  g_large_alloc_lock.unlock();

  return static_cast<uint8_t *>(virt_ptr) + sizeof(LargeAllocHeader);
}

void free_large(void *ptr) noexcept {
  const VirtualAddress virt = VirtualAddress::from_ptr(ptr) - sizeof(LargeAllocHeader);
  auto *header = virt.as_ptr<LargeAllocHeader>();

  g_large_alloc_lock.lock();

  if (header->magic != kLargeAllocMagic) [[unlikely]] {
    g_large_alloc_lock.unlock();
    early_print_fmt("free_large: invalid header at {}\n\r", ptr);
    return;
  }

  const size_t pages = header->pages;
  const PhysicalAddress phys = HAL_VMM_GetPhysicalAddress(virt);

  for (size_t i = 0; i < pages; ++i) {
    HAL_VMM_UnmapPage(virt + i * g_page_size);
  }

  HAL_PMM_FreeFrames(phys, pages);
  KE_VMM_FreeRegion(KE_VMM_GetKernelAddressSpace(), virt.as_ptr());

  g_large_alloc_lock.unlock();
}

void Allocator::free_slab(Slab &slab) noexcept {
  g_registry_lock.lock();
  g_global_registry.remove(&slab);
  g_registry_lock.unlock();

  total_allocated_ -= layout_.slab_size;

  const VirtualAddress virt = VirtualAddress::from_ptr(slab.memory().data());
  const PhysicalAddress phys = HAL_VMM_GetPhysicalAddress(virt);

  const size_t pages = slab.memory().size() / layout_.page_size;
  for (size_t i = 0; i < pages; ++i) {
    HAL_VMM_UnmapPage(virt + i * layout_.page_size);
  }

  HAL_PMM_FreeFrames(phys, pages);
}


}// namespace kernel::memory
