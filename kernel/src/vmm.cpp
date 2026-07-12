#include "vmm.hpp"
#include "stdkrn.hpp"

#include <kernel/hal_interface.hpp>

#include "memory/Cache.hpp"

extern uint32_t g_page_size;

//#define KALLOC_DEBUG 1

namespace {
struct KVmmObject final : kernel::KObject {
  KVmmObject() : KObject(kernel::TYPE_VMM, "VMM") {}
};

constexpr size_t g_cache_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};

kernel::vmm::AddressSpace g_kernel_address_space;
kstd::array<kernel::memory::Cache, 8> g_malloc_caches;
}// namespace

namespace kernel::vmm {
void init() noexcept {
  // Start heap after the last mapped region + one guard page
  const VirtualAddress heap_start = (HAL_VMM_GetHighestVirtualAddress() + g_page_size - 1)
                                        .align_down(g_page_size) +
                                    g_page_size;

  // Initialize kernel address space
  g_kernel_address_space.translation_root = HAL_VMM_GetCurrentTranslationRoot();
  g_kernel_address_space.asid = 0;
  g_kernel_address_space.current_base = nullptr;
  g_kernel_address_space.limit = nullptr;
  g_kernel_address_space.current_base = (heap_start + 0x10000000).align_up(0x1000000);
  g_kernel_address_space.limit = 0xFF000000;

  memory::set_heap_start(heap_start);
  for (size_t i = 0; i < g_malloc_caches.size(); ++i) {
    new (&g_malloc_caches[i]) memory::Cache(g_cache_sizes[i], g_cache_sizes[i], g_page_size);
  }

  early_print_fmt("UnDOS Heap initialized at {}\r\n", heap_start.as_ptr<>());
}

void late_init() noexcept {
  // Register VMM
  KE_OB_InsertObject(KE_OB_GetMemoryDirectory(), kernel::CreateKObject<KVmmObject>("VMM"));

  early_print_fmt("VMM: Registered with OM\r\n");
}

bool create_user_address_space(AddressSpace &out_as) noexcept {
  const PhysicalAddress root = HAL_VMM_CreateAddressSpace();
  if (!root) {
    return false;
  }

  out_as.vads = VadTree();
  out_as.translation_root = root;
  out_as.asid = 0;
  out_as.current_base = 0x10000000;
  out_as.limit = 0xC0000000;

  return true;
}

void destroy_user_address_space(AddressSpace &as) noexcept {
  if (!as.translation_root) {
    return;
  }

  // Reclaim the physical frames backing each mapped region while the target
  // address space is active, since HAL_VMM_GetPhysicalAddress walks whatever
  // page directory CR3 currently points at.
  const PhysicalAddress caller_root = HAL_VMM_GetCurrentTranslationRoot();
  HAL_CPU_DisableInterrupts();

  HAL_VMM_SwitchAddressSpace(as.translation_root);

  as.vads.clear([](VirtualAddressDescriptor *vad) noexcept {
    for (VirtualAddress page = vad->start; page < vad->end; page += g_page_size) {
      if (const PhysicalAddress phys = HAL_VMM_GetPhysicalAddress(page)) {
        HAL_PMM_FreeFrames(phys, 1);
      }
    }
    KE_Free(vad);
  });

  HAL_VMM_SwitchAddressSpace(caller_root);
  HAL_CPU_EnableInterrupts();

  HAL_VMM_DestroyAddressSpace(as.translation_root);
  as.translation_root = PhysicalAddress(0);
}

void *allocate_user_memory(AddressSpace &as, size_t size, ProtectFlags flags) noexcept {
  const size_t count = (size + g_page_size - 1) / g_page_size;
  const PhysicalAddress phys = HAL_PMM_AllocateFrames(count);
  if (!phys) return nullptr;

  // Use the AddressSpace's VAD tree to find a free region
  void *virt = KE_VMM_AllocateRegion(as, size, flags);
  if (!virt) {
    HAL_PMM_FreeFrames(phys, count);
    return nullptr;
  }

  const VirtualAddress v_addr = VirtualAddress::from_ptr(virt);
  for (size_t i = 0; i < count; ++i) {
    if (!HAL_VMM_MapPage(v_addr + i * g_page_size, phys + i * g_page_size, flags | ProtectFlags::USER)) {
      return nullptr;
    }
    HAL_VMM_Flush(v_addr + i * g_page_size);
  }

  return virt;
}
}// namespace kernel::vmm

UNDOS_KERNEL_API_DEF void *KE_Malloc(size_t size) noexcept {
  if (size == 0) [[unlikely]] {
    return nullptr;
  }

  for (auto &g_malloc_cache: g_malloc_caches) {
    if (size <= g_malloc_cache.object_size()) {
      auto allocate = g_malloc_cache.allocate();
#if defined(KALLOC_DEBUG) && KALLOC_DEBUG == 1
      early_print_fmt("ALLOC: {} @ {}\r\n", size, allocate);
#endif
      return allocate;
    }
  }

  // Larger than the biggest fixed-size cache: fall back to a dedicated,
  // page-backed allocation instead of failing outright.
  if (auto *large = kernel::memory::allocate_large(size)) {
#if defined(KALLOC_DEBUG) && KALLOC_DEBUG == 1
    early_print_fmt("ALLOC LARGE: {} @ {}\r\n", size, large);
#endif
    return large;
  }

  early_print_fmt("ALLOC FAILED: {}\r\n", size);
  return nullptr;
}

UNDOS_KERNEL_API_DEF void KE_Free(void *ptr) noexcept {
  if (!ptr) [[unlikely]] { return; }

  if (auto *slab = kernel::memory::find_slab(ptr)) {
#if defined(KALLOC_DEBUG) && KALLOC_DEBUG == 1
    early_print_fmt("FREE SLAB: {}\r\n", ptr);
#endif
    slab->cache()->free(ptr, *slab);
    return;
  }

  kernel::memory::free_large(ptr);
}

UNDOS_KERNEL_API_DEF void *KE_VMM_AllocateRegion(kernel::vmm::AddressSpace &as, size_t size, kernel::vmm::ProtectFlags flags) noexcept {
  if (as.current_base == 0) {
    // Default to user heap range if not initialized
    as.current_base = 0x10000000;
    as.limit = 0xC0000000;
  }

  const kernel::VirtualAddress addr = as.current_base;
  const size_t aligned_size = (size + g_page_size - 1) & ~(g_page_size - 1);

  if (as.current_base + aligned_size > as.limit) {
    return nullptr;
  }

  as.current_base += aligned_size;

  if (auto *vad = KE_CreateObject<kernel::vmm::VirtualAddressDescriptor>()) {
    vad->start = addr;
    vad->end = addr + size;
    vad->flags = flags;
    as.vads.insert(*vad);
  }

  return addr.as_ptr();
}

UNDOS_KERNEL_API_DEF void KE_VMM_FreeRegion(kernel::vmm::AddressSpace &as, void *addr) noexcept {
  auto *vad = as.vads.find(kernel::VirtualAddress::from_ptr(addr));
  if (vad) {
    as.vads.remove(vad);
    KE_Free(vad);
  }
}

UNDOS_KERNEL_API_DEF void *KE_VMM_AllocateUserData(kernel::vmm::AddressSpace &as, size_t size) noexcept {
  return kernel::vmm::allocate_user_memory(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
}

UNDOS_KERNEL_API_DEF void *KE_VMM_AllocateUserProcess(kernel::vmm::AddressSpace &as, size_t size) noexcept {
  return kernel::vmm::allocate_user_memory(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::EXECUTE);
}

UNDOS_KERNEL_API_DEF kernel::vmm::AddressSpace &KE_VMM_GetKernelAddressSpace() noexcept {
  return g_kernel_address_space;
}

UNDOS_KERNEL_API_DEF bool KE_VMM_MapPhysical(kernel::vmm::AddressSpace &as, kernel::VirtualAddress virt, kernel::PhysicalAddress phys, kernel::vmm::ProtectFlags flags) noexcept {
  (void) as;// TODO: Use the address space's translation root if not current
  return HAL_VMM_MapPage(virt, phys, flags);
}


// On a hosted build (e.g. the host test binary), the C++ runtime/standard
// library already provides its own global operator new/delete backed by the
// host's general-purpose allocator; overriding them here with KE_Malloc/Free
// (which only serve a handful of small, fixed object sizes and require
// kernel::vmm::init() to have run first) would break every other allocation
// in the process, including ones made before init() runs.
#if __STDC_HOSTED__ == 0
UNDOS_KERNEL_CPP_API void *operator new(size_t size) { return KE_Malloc(size); }
UNDOS_KERNEL_CPP_API void *operator new[](size_t size) { return KE_Malloc(size); }
UNDOS_KERNEL_CPP_API void operator delete(void *ptr) noexcept { KE_Free(ptr); }
UNDOS_KERNEL_CPP_API void operator delete[](void *ptr) noexcept { KE_Free(ptr); }
UNDOS_KERNEL_CPP_API void operator delete(void *ptr, size_t) noexcept { KE_Free(ptr); }
UNDOS_KERNEL_CPP_API void operator delete[](void *ptr, size_t) noexcept { KE_Free(ptr); }
#endif
