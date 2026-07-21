
#include "Allocator.hpp"
#include "Cache.hpp"
#include "../vmm.hpp"
#include <kernel/hal_interface.hpp>
#include <new.hpp>

extern uint32_t g_page_size;

namespace kernel::memory {
namespace {
adt::AvlTree<Slab, &Slab::avl_node> g_global_registry;
VirtualAddress g_kernel_heap_current = 0;

constexpr uint32_t kLargeAllocMagic = 0x4C524745;// 'LRGE'

struct LargeAllocHeader {
  uint32_t magic;
  size_t pages;
};
}// namespace

void set_heap_start(VirtualAddress start) noexcept {
  g_kernel_heap_current = start;
}

Slab *find_slab(void *ptr) noexcept {
  return g_global_registry.find(reinterpret_cast<uintptr_t>(ptr));
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

  g_global_registry.insert(*slab);
  total_allocated_ += layout_.slab_size;

  return slab;
}

void *allocate_large(size_t size) noexcept {
  const size_t total = size + sizeof(LargeAllocHeader);
  
  void *virt_ptr = vmm::allocate_memory(
      KE_VMM_GetKernelAddressSpace(), total,
      vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE);

  if (!virt_ptr) {
    early_print_fmt("Failed to allocate large allocation!\n\r");
    return nullptr;
  }

  auto *header = static_cast<LargeAllocHeader *>(virt_ptr);
  header->magic = kLargeAllocMagic;
  header->pages = (total + g_page_size - 1) / g_page_size;

  return static_cast<uint8_t *>(virt_ptr) + sizeof(LargeAllocHeader);
}

void free_large(void *ptr) noexcept {
  const VirtualAddress virt = VirtualAddress::from_ptr(ptr) - sizeof(LargeAllocHeader);
  auto *header = virt.as_ptr<LargeAllocHeader>();
  if (header->magic != kLargeAllocMagic) [[unlikely]] {
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
}

void Allocator::free_slab(Slab &slab) noexcept {
  g_global_registry.remove(&slab);

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
