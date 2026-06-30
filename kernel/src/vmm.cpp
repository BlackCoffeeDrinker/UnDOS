#include "vmm.hpp"

#include "object_manager.hpp"
#include <kernel/hal_interface.hpp>

#include "common/DoubleList.hpp"
#include "memory/Cache.hpp"

namespace {
kernel::vmm::AddressSpace g_kernel_address_space;
uint32_t g_page_size = 4096;

kernel::memory::Cache g_malloc_caches[8];
const size_t g_cache_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};

struct KVmmObject final : kernel::KObject {
  KVmmObject() : KObject(kernel::TYPE_VMM) {}
};
}// namespace

namespace kernel::vmm {
void init(const BootInfoT &boot_info) noexcept {
  // Initialize kernel address space
  g_kernel_address_space.translation_root = HAL_VMM_GetCurrentTranslationRoot();
  g_kernel_address_space.asid = 0;

  // Find the highest virtual address space
  VirtualAddress highest_addr = 0;
  for (const auto &region: boot_info.mapped_memory) {
    if (region.type == MappedMemoryRegionType::None) continue;
    if (VirtualAddress end = region.virtual_base + region.length;
        end > highest_addr) {
      highest_addr = end;
    }
  }

  // Start heap after the last mapped region + one guard page
  VirtualAddress heap_start = (highest_addr + boot_info.page_size - 1).align_down(boot_info.page_size);
  heap_start += boot_info.page_size;
  g_page_size = boot_info.page_size;

  memory::set_heap_start(heap_start);

  for (size_t i = 0; i < 8; ++i) {
    new (&g_malloc_caches[i]) memory::Cache(g_cache_sizes[i], g_cache_sizes[i], boot_info.page_size);
  }
}

void late_init() noexcept {
  // Register VMM
  if (const auto memory_dir = static_cast<KDirectoryObject *>(KE_Ob_LookupObject("\\Memory").get())) {
    if (const auto vmm_obj = KE_CreateObject<KVmmObject>()) {
      vmm_obj->name = "VMM";
      KE_Ob_InsertObject(memory_dir, vmm_obj);
    }
  }
}

UNDOS_KERNEL_API void *KE_Malloc(size_t size) noexcept {
  if (size == 0) [[unlikely]] {
    return nullptr;
  }

  for (size_t i = 0; i < 8; ++i) {
    if (size <= g_cache_sizes[i]) {
      return g_malloc_caches[i].allocate();
    }
  }

  return nullptr;
}

UNDOS_KERNEL_API void KE_Free(void *ptr) noexcept {
  if (!ptr) [[unlikely]] {
    return;
  }

  if (auto *slab = memory::find_slab(ptr)) {
    slab->cache()->free(ptr, slab);
  }
}

AddressSpace *get_kernel_address_space() noexcept {
  return &g_kernel_address_space;
}

void *AddressSpace::allocate_region(size_t size, ProtectFlags flags) noexcept {
  // Basic implementation: use a simple bump allocator for user space for now.
  // In a real OS, this would search the VAD tree for a hole.
  static VirtualAddress user_heap_base = 0x10000000;

  const VirtualAddress addr = user_heap_base;
  user_heap_base += (size + g_page_size - 1) & ~(g_page_size - 1);

  if (auto *vad = KE_CreateObject<VirtualAddressDescriptor>()) {
    vad->start = addr;
    vad->end = addr + size;
    vad->flags = flags;
    vads.insert(vad);
  }

  return addr.as_ptr();
}

void AddressSpace::free_region(void *addr) noexcept {
  auto *vad = vads.find(VirtualAddress::from_ptr(addr));
  if (vad) {
    vads.remove(vad);
    KE_Free(vad);
  }
}

void *AddressSpace::allocate_user_data(size_t size) noexcept {
  return allocate_user_memory(*this, size, ProtectFlags::READ | ProtectFlags::WRITE);
}

void *AddressSpace::allocate_user_process(size_t size) noexcept {
  return allocate_user_memory(*this, size, ProtectFlags::READ | ProtectFlags::EXECUTE);
}


void *allocate_user_memory(vmm::AddressSpace &as, size_t size, vmm::ProtectFlags flags) noexcept {
  const size_t count = (size + g_page_size - 1) / g_page_size;

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
    if (!HAL_VMM_MapPage(v_addr + i * g_page_size, phys + i * g_page_size, flags | vmm::ProtectFlags::USER)) {
      return nullptr;
    }
    HAL_VMM_Flush(v_addr + i * g_page_size);
  }

  return virt;
}
}// namespace kernel::vmm


UNDOS_KERNEL_CPP_API void *operator new(size_t size) {
  return KE_Malloc(size);
}

UNDOS_KERNEL_CPP_API void *operator new[](size_t size) {
  return KE_Malloc(size);
}

UNDOS_KERNEL_CPP_API void operator delete(void *ptr) noexcept {
  KE_Free(ptr);
}

UNDOS_KERNEL_CPP_API void operator delete[](void *ptr) noexcept {
  KE_Free(ptr);
}

UNDOS_KERNEL_CPP_API void operator delete(void *ptr, size_t) noexcept {
  KE_Free(ptr);
}

UNDOS_KERNEL_CPP_API void operator delete[](void *ptr, size_t) noexcept {
  KE_Free(ptr);
}
