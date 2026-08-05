#include "vmm.hpp"
#include "stdkrn.hpp"

#include <kernel/hal_interface.hpp>

#include "memory/Cache.hpp"

extern uint32_t g_page_size;

//#define KALLOC_DEBUG 1

namespace {

constexpr size_t g_cache_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};

kernel::vmm::AddressSpace g_kernel_address_space{};
kstd::array<kernel::memory::Cache, 8> g_malloc_caches;
}// namespace

namespace kernel::vmm {
void init() noexcept {
  for (size_t i = 0; i < g_malloc_caches.size(); ++i) {
    new (&g_malloc_caches[i]) memory::Cache(g_cache_sizes[i], g_cache_sizes[i], g_page_size);
  }
}

void late_init() noexcept {
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
    // Explicitly run the destructor to release the VAD's KObjectPtr<KMemoryBackingObject>
    // reference before returning the raw storage to the allocator.
    vad->~VirtualAddressDescriptor();
    KE_Free(vad);
  });

  HAL_VMM_SwitchAddressSpace(caller_root);
  HAL_CPU_EnableInterrupts();

  HAL_VMM_DestroyAddressSpace(as.translation_root);
  as.translation_root = PhysicalAddress(0);
}

void *allocate_memory(AddressSpace &as, size_t size, ProtectFlags flags) noexcept {
  const size_t count = (size + g_page_size - 1) / g_page_size;
  const PhysicalAddress phys = HAL_PMM_AllocateFrames(count);
  if (!phys) return nullptr;

  // Use the AddressSpace's VAD tree to find a free region
  void *virt = KE_VMM_AllocateRegion(as, size, flags);
  if (!virt) {
    HAL_PMM_FreeFrames(phys, count);
    return nullptr;
  }

  // HAL_VMM_MapPage always writes into whichever page directory is
  // currently active (CR3), not into an arbitrary AddressSpace, so the
  // target address space must be made active while the pages are mapped --
  // mirrors the pattern used by destroy_user_address_space. Without this,
  // callers that haven't already switched to `as` (e.g. the process's user
  // stack, mapped before its address space is switched in) would silently
  // map the pages into the caller's (kernel) address space instead.
  const PhysicalAddress caller_root = HAL_VMM_GetCurrentTranslationRoot();
  const bool switch_as = as.translation_root && as.translation_root != caller_root;
  if (switch_as) {
    HAL_CPU_DisableInterrupts();
    HAL_VMM_SwitchAddressSpace(as.translation_root);
  }

  bool ok = true;
  const VirtualAddress v_addr = VirtualAddress::from_ptr(virt);
  const ProtectFlags map_flags = (&as != &g_kernel_address_space) ? (flags | ProtectFlags::USER) : flags;
  for (size_t i = 0; i < count; ++i) {
    if (!HAL_VMM_MapPage(v_addr + i * g_page_size, phys + i * g_page_size, map_flags)) {
      ok = false;
      break;
    }
    HAL_VMM_Flush(v_addr + i * g_page_size);
  }

  if (switch_as) {
    HAL_VMM_SwitchAddressSpace(caller_root);
    HAL_CPU_EnableInterrupts();
  }

  if (!ok) {
    HAL_PMM_FreeFrames(phys, count);
    return nullptr;
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

namespace kernel::vmm {
size_t debug_slab_cache_allocated_bytes(size_t size) noexcept {
  for (auto &g_malloc_cache: g_malloc_caches) {
    if (size <= g_malloc_cache.object_size()) {
      return g_malloc_cache.allocated_bytes();
    }
  }
  return 0;
}
}// namespace kernel::vmm

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

namespace {
// Shared address-bump/limit-check logic used by every VAD-reserving
// allocator. Bumps `as.current_base` by the page-aligned size and creates a
// bare VAD spanning the reserved range, or returns nullptr on address-space
// exhaustion. Callers are responsible for setting `vad->backing` (if any).
kernel::vmm::VirtualAddressDescriptor *reserve_vad(kernel::vmm::AddressSpace &as, size_t size, kernel::vmm::ProtectFlags flags) noexcept {
  if (as.current_base == 0) {
    HAL_PLATFORM_Panic("VMM: Address space not initialized", __FILE__, __LINE__);
  }

  const kernel::VirtualAddress addr = as.current_base;
  const size_t aligned_size = (size + g_page_size - 1) & ~(g_page_size - 1);

  if (as.current_base + aligned_size > as.limit) {
    return nullptr;
  }

  as.current_base += aligned_size;

  if (auto vad = KE_CreateObject<kernel::vmm::VirtualAddressDescriptor>()) {
    vad->start = addr;
    vad->end = addr + size;
    vad->flags = flags;

    auto *rawvad = vad.release();
    as.vads.insert(*rawvad);
    return rawvad;
  }

  return nullptr;
}
}// namespace

UNDOS_KERNEL_API_DEF void *KE_VMM_AllocateRegion(kernel::vmm::AddressSpace &as, size_t size, kernel::vmm::ProtectFlags flags) noexcept {
  auto *vad = reserve_vad(as, size, flags);
  if (!vad) {
    return nullptr;
  }
  return vad->start.as_ptr();
}

UNDOS_KERNEL_API_DEF kernel::vmm::VirtualAddressDescriptor *KE_VMM_MapBackingObject(kernel::vmm::AddressSpace &as, const kernel::KObjectPtr<kernel::KMemoryBackingObject> &backing, size_t size, kernel::vmm::ProtectFlags flags) noexcept {
  auto *vad = reserve_vad(as, size, flags);
  if (!vad) {
    return nullptr;
  }
  vad->backing = backing;
  return vad;
}

UNDOS_KERNEL_API_DEF void KE_VMM_FreeRegion(kernel::vmm::AddressSpace &as, void *addr) noexcept {
  if (auto *vad = as.vads.find(kernel::VirtualAddress::from_ptr(addr))) {
    as.vads.remove(vad);
    // Explicitly run the destructor to release the VAD's KObjectPtr<KMemoryBackingObject>
    // reference before returning the raw storage to the allocator.
    vad->~VirtualAddressDescriptor();
    KE_Free(vad);
  }
}

namespace {
// Creates and registers a fresh anonymous BackingObject sized to `size`
// under \Memory, giving each segment a uniquely named, inspectable object.
kernel::KObjectPtr<kernel::KMemoryBackingObject> create_anonymous_backing(size_t size, const char *prefix) noexcept {
  static uint32_t next_id = 0;
  kstd::static_string<64> name;
  kstd::format(name, "{}{}", prefix, next_id++);

  auto backing = kernel::CreateKObject<kernel::KMemoryBackingObject>(name);
  if (!backing) return nullptr;

  backing->kind = kernel::KMemoryBackingObject::Kind::Anonymous;
  backing->size = size;
  KE_OB_InsertObject(KE_OB_GetMemoryDirectory(), backing);
  return backing;
}
}// namespace

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KMemoryBackingObject> KE_VMM_CreateDirectBacking(kernel::VirtualAddress directAddress, size_t size) noexcept {
  static uint32_t next_id = 0;
  kstd::static_string<64> name;
  kstd::format(name, "DirectBacking{}", next_id++);

  auto backing = kernel::CreateKObject<kernel::KMemoryBackingObject>(name);
  if (!backing) return nullptr;

  backing->kind = kernel::KMemoryBackingObject::Kind::Anonymous;
  backing->size = size;
  backing->directAddress = directAddress;
  KE_OB_InsertObject(KE_OB_GetMemoryDirectory(), backing);
  return backing;
}

UNDOS_KERNEL_API_DEF void *KE_VMM_AllocateUserData(kernel::vmm::AddressSpace &as, size_t size) noexcept {
  // Demand-paged: reserve the VAD (backed by a fresh Anonymous
  // BackingObject) without eagerly allocating/mapping any physical frames.
  // The pages are populated lazily by KE_VMM_HandlePagefault on first touch.
  auto backing = create_anonymous_backing(size, "UserData");
  if (!backing) return nullptr;

  auto *vad = KE_VMM_MapBackingObject(as, backing, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE | kernel::vmm::ProtectFlags::USER);
  if (!vad) return nullptr;
  return vad->start.as_ptr();
}

UNDOS_KERNEL_API_DEF void *KE_VMM_AllocateUserProcess(kernel::vmm::AddressSpace &as, size_t size) noexcept {
  // Demand-paged: reserve the VAD (backed by a fresh Anonymous
  // BackingObject) without eagerly allocating/mapping any physical frames.
  // The pages are populated lazily by KE_VMM_HandlePagefault on first touch.
  auto backing = create_anonymous_backing(size, "UserProcess");
  if (!backing) return nullptr;

  auto *vad = KE_VMM_MapBackingObject(as, backing, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::EXECUTE | kernel::vmm::ProtectFlags::USER);
  if (!vad) return nullptr;
  return vad->start.as_ptr();
}

UNDOS_KERNEL_API_DEF kernel::vmm::AddressSpace &KE_VMM_GetKernelAddressSpace() noexcept {
  return g_kernel_address_space;
}

UNDOS_KERNEL_API_DEF bool KE_VMM_MapPhysical(kernel::vmm::AddressSpace &as, kernel::VirtualAddress virt, kernel::PhysicalAddress phys, kernel::vmm::ProtectFlags flags) noexcept {
  (void) as;// TODO: Use the address space's translation root if not current
  return HAL_VMM_MapPage(virt, phys, flags);
}

UNDOS_KERNEL_API_DEF void *KE_VMM_MapBorrowed(void *addr, size_t size, kernel::vmm::AddressSpace &as) noexcept {
  if (!addr || size == 0) {
    return nullptr;
  }

  const kernel::VirtualAddress user_addr = kernel::VirtualAddress::from_ptr(addr);
  const kernel::VirtualAddress page_start = user_addr.align_down(g_page_size);
  const kernel::VirtualAddress page_end = (user_addr + size).align_up(g_page_size);
  const size_t page_count = (page_end.value - page_start.value) / g_page_size;
  const size_t offset = user_addr.value - page_start.value;

  auto *phys_pages = static_cast<kernel::PhysicalAddress *>(KE_Malloc(page_count * sizeof(kernel::PhysicalAddress)));
  if (!phys_pages) {
    return nullptr;
  }

  // Walk the source address space's page tables while it's active to
  // capture the physical frames backing the borrowed range.
  const kernel::PhysicalAddress caller_root = HAL_VMM_GetCurrentTranslationRoot();
  const bool switch_to_source = as.translation_root && as.translation_root != caller_root;
  if (switch_to_source) {
    HAL_CPU_DisableInterrupts();
    HAL_VMM_SwitchAddressSpace(as.translation_root);
  }

  bool ok = true;
  for (size_t i = 0; i < page_count; ++i) {
    const kernel::PhysicalAddress phys = HAL_VMM_GetPhysicalAddress(page_start + i * g_page_size);
    if (!phys) {
      ok = false;
      break;
    }
    phys_pages[i] = phys;
  }

  if (switch_to_source) {
    HAL_VMM_SwitchAddressSpace(caller_root);
    HAL_CPU_EnableInterrupts();
  }

  if (!ok) {
    KE_Free(phys_pages);
    return nullptr;
  }

  kernel::vmm::AddressSpace &kernel_as = KE_VMM_GetKernelAddressSpace();
  void *kernel_region = KE_VMM_AllocateRegion(kernel_as, page_count * g_page_size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!kernel_region) {
    KE_Free(phys_pages);
    return nullptr;
  }

  const kernel::VirtualAddress kernel_start = kernel::VirtualAddress::from_ptr(kernel_region);
  for (size_t i = 0; i < page_count; ++i) {
    if (!HAL_VMM_MapPage(kernel_start + i * g_page_size, phys_pages[i], kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE)) {
      ok = false;
      break;
    }
    HAL_VMM_Flush(kernel_start + i * g_page_size);
  }

  KE_Free(phys_pages);

  if (!ok) {
    KE_VMM_FreeRegion(kernel_as, kernel_region);
    return nullptr;
  }

  return (kernel_start + offset).as_ptr();
}

UNDOS_KERNEL_API_DEF void KE_VMM_UnmapBorrowed(void *addr, size_t size, kernel::vmm::AddressSpace &as) noexcept {
  (void) as;// The borrow is only ever mapped into the kernel address space.
  if (!addr) {
    return;
  }

  const kernel::VirtualAddress mapped_addr = kernel::VirtualAddress::from_ptr(addr);
  const kernel::VirtualAddress page_start = mapped_addr.align_down(g_page_size);
  const kernel::VirtualAddress page_end = (mapped_addr + size).align_up(g_page_size);
  const size_t page_count = (page_end.value - page_start.value) / g_page_size;

  for (size_t i = 0; i < page_count; ++i) {
    HAL_VMM_UnmapPage(page_start + i * g_page_size);
    HAL_VMM_Flush(page_start + i * g_page_size);
  }

  KE_VMM_FreeRegion(KE_VMM_GetKernelAddressSpace(), page_start.as_ptr());
}

UNDOS_KERNEL_API_DEF void KE_VMM_HandlePagefault(const kernel::PageFaultInfo &info) noexcept {
  // Generic, architecture-agnostic page-fault dispatcher. The HAL has
  // already decoded the fault registers (faulting address, error code,
  // privilege level, faulting IP) into `info`; this is the single place
  // where all platforms funnel into for demand paging / copy-on-write /
  // pagefile handling.
  early_print_fmt(
      "Page Fault: addr={} ip={} present={} write={} user={} exec={}\r\n",
      info.fault_addr.as_ptr<>(), info.ip.as_ptr<>(),
      info.present, info.write, info.user, info.exec);

  // 1. Identify the address space the fault occurred in: a user-mode fault
  // belongs to the currently running thread's owning process, while a
  // kernel-mode fault (or a user fault with no owning process, which should
  // never happen) is resolved against the kernel address space.
  const kernel::vmm::AddressSpace *as = &KE_VMM_GetKernelAddressSpace();
  if (info.user) {
    if (const auto current_thread = KE_SCHED_GetCurrentThread(); current_thread && current_thread->owner_process) {
      as = &current_thread->owner_process->address_space;
    } else {
      HAL_PLATFORM_Panic("Page Fault: user-mode fault with no owning process", __FILE__, __LINE__);
    }
  }

  // 2. Look up the VAD covering the faulting address in that address space.
  // No VAD means the access targets memory that was never reserved for this
  // address space -- an invalid access that must be fatal.
  const auto *vad = as->vads.find(info.fault_addr);
  if (!vad) {
    HAL_PLATFORM_Panic("Page Fault: access to unmapped/invalid address", __FILE__, __LINE__);
  }

  // 3. Protection checks: reject accesses that violate the VAD's declared
  // permissions before doing any mapping work. These are fatal, since they
  // indicate either a user-mode bug (illegal access) or a kernel bug
  // (accessing user memory incorrectly).
  if (info.write && !kernel::vmm::has_flag(vad->flags, kernel::vmm::ProtectFlags::WRITE)) {
    HAL_PLATFORM_Panic("Page Fault: write fault on non-writable region", __FILE__, __LINE__);
  }
  if (info.exec && !kernel::vmm::has_flag(vad->flags, kernel::vmm::ProtectFlags::EXECUTE)) {
    HAL_PLATFORM_Panic("Page Fault: exec fault on non-executable region", __FILE__, __LINE__);
  }
  if (info.user && !kernel::vmm::has_flag(vad->flags, kernel::vmm::ProtectFlags::USER)) {
    HAL_PLATFORM_Panic("Page Fault: user-mode fault on kernel-only region", __FILE__, __LINE__);
  }

  // 4. A VAD exists for the faulting address and the access is permitted by
  // its protection flags; decide how to satisfy the fault based on what
  // backs the region. VADs without a distinct BackingObject (e.g.
  // lightweight kernel-internal scratch mappings created via the thin
  // KE_VMM_AllocateRegion wrapper) are treated as anonymous.
  const kernel::KMemoryBackingObject::Kind kind =
      vad->backing ? vad->backing->kind : kernel::KMemoryBackingObject::Kind::Anonymous;
  switch (kind) {
    case kernel::KMemoryBackingObject::Kind::Anonymous: {
      // Demand-paged memory: allocate a fresh physical frame to back the
      // faulting page.
      const kernel::PhysicalAddress phys = HAL_PMM_AllocateFrames(1);
      if (!phys) {
        HAL_PLATFORM_Panic("Page Fault: out of physical memory", __FILE__, __LINE__);
      }
      if (const auto page_base = info.fault_addr.align_down(g_page_size);
          !HAL_VMM_MapPage(page_base, phys, vad->flags)) {
        HAL_PLATFORM_Panic("Page Fault: HAL_VMM_MapPage failed", __FILE__, __LINE__);
      }
      break;
    }
    case kernel::KMemoryBackingObject::Kind::File:
      // TODO: read the page's contents from backing->file at
      // backing->file_offset + page_index * PAGE_SIZE once VFS read
      // integration is wired up here. For now, this is just a stub.
      HAL_PLATFORM_Panic("Page Fault: file-backed read not implemented", __FILE__, __LINE__);
      break;
    case kernel::KMemoryBackingObject::Kind::Shared:
      // TODO: shared-memory semantics (mirrors Anonymous/File depending on
      // design) are not implemented yet.
      HAL_PLATFORM_Panic("Page Fault: shared-memory fault not implemented", __FILE__, __LINE__);
      break;
    case kernel::KMemoryBackingObject::Kind::KernelModule:
      // TODO: file-backed kernel module page-in not implemented yet.
      HAL_PLATFORM_Panic("Page Fault: kernel-module-backed fault not implemented", __FILE__, __LINE__);
      break;
  }
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
