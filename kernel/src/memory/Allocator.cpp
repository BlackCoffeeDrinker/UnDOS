
#include "Allocator.hpp"
#include "Cache.hpp"
#include <kernel/hal_interface.hpp>
#include <new.hpp>

namespace kernel::memory {
namespace {
adt::AvlTree<Slab, &Slab::avl_node> g_global_registry;
VirtualAddress g_kernel_heap_current = 0;
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
  if (!phys) return nullptr;

  VirtualAddress virt = g_kernel_heap_current;
  g_kernel_heap_current += layout_.slab_size;

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
