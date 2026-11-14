#include "vmm.hpp"
#include "stdkrn.hpp"

#include <kernel/hal_interface.hpp>

#include "memory/Cache.hpp"
#include "vmm_owner_lifecycle.hpp"
#include "process.hpp"

extern uint32_t g_page_size;

#if __STDC_HOSTED__
namespace kernel::vmm::test_hooks {
void (*after_borrowed_claim)() = nullptr;
void (*before_borrowed_reserve)() = nullptr;
void (*before_borrowed_map)(VirtualAddressDescriptor *) = nullptr;
}// namespace kernel::vmm::test_hooks
#endif

//#define KALLOC_DEBUG 1

namespace {

constexpr size_t g_cache_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};

kernel::vmm::AddressSpace g_kernel_address_space{};
kstd::array<kernel::memory::Cache, 8> g_malloc_caches;
kstd::expected<void *, kstd::error_code> allocate_user_backed_region(
    kernel::vmm::AddressSpace &as, size_t size, const char *name,
    kernel::vmm::ProtectFlags flags, const char *operation) noexcept;
}// namespace

namespace kernel::detail {
void rw_mutex_relax() noexcept { KE_SCHED_Yield(); }
uint32_t rw_load(const uint32_t *word) noexcept {
  return HAL_ATOMIC_Load32(VirtualAddress::from_ptr(word), PlatformMemoryOrder::Acquire);
}
void rw_store(uint32_t *word, uint32_t value) noexcept {
  HAL_ATOMIC_Store32(VirtualAddress::from_ptr(word), value, PlatformMemoryOrder::Release);
}
bool rw_cas(uint32_t *word, uint32_t &expected, uint32_t desired) noexcept {
  return HAL_ATOMIC_CompareExchange32(VirtualAddress::from_ptr(word), expected, desired, PlatformMemoryOrder::AcquireRelease);
}
void rw_add(uint32_t *word, uint32_t delta) noexcept {
  HAL_ATOMIC_FetchAdd32(VirtualAddress::from_ptr(word), delta, PlatformMemoryOrder::Release);
}
}// namespace kernel::detail

namespace {
// User address spaces are guarded by their own RwMutex; the kernel one by the
// arena mutex (its VAD tree backs the heap, which must not re-enter).
bool is_user_as(const kernel::vmm::AddressSpace &as) noexcept { return &as != &g_kernel_address_space; }
}// namespace

namespace kernel::vmm {
void init() noexcept {
  for (size_t i = 0; i < g_malloc_caches.size(); ++i) {
    new (&g_malloc_caches[i]) memory::Cache(g_cache_sizes[i], g_cache_sizes[i], g_page_size);
  }
}

void late_init() noexcept {
}

kstd::error_code create_user_address_space(AddressSpace &out_as, uint32_t owner_pid) noexcept {
  return detail::create_owned_address_space_root(
      out_as, owner_pid,
      [](uint32_t pid) noexcept { return HAL_PMM_RegisterOwner(pid); },
      []() noexcept { return HAL_VMM_CreateAddressSpace(); },
      [](PhysicalAddress root, uint32_t pid) noexcept {
        return HAL_VMM_SetAddressSpaceOwner(root, pid);
      },
      [](PhysicalAddress root) noexcept { HAL_VMM_DestroyAddressSpace(root); },
      [](uint32_t pid) noexcept { HAL_PMM_UnregisterOwner(pid); });
}

void destroy_user_address_space(AddressSpace &as) noexcept {
  if (!as.translation_root || as.translation_root == PhysicalAddress::Invalid) {
    return;
  }

  // Reclaim the physical frames backing each mapped region while the target
  // address space is active, since HAL_VMM_GetPhysicalAddress walks whatever
  // page directory CR3 currently points at.
  ExclusiveGuard guard(as.lock, is_user_as(as));
  const PhysicalAddress caller_root = HAL_VMM_GetCurrentTranslationRoot();
  const uintptr_t irq_state = HAL_CPU_SaveAndDisableInterrupts();

  HAL_VMM_SwitchAddressSpace(as.translation_root);

  as.vads.clear([&as](VirtualAddressDescriptor *vad) noexcept {
    if (vad->kind != kernel::vmm::VadKind::Ordinary) {
      HAL_PLATFORM_Panic("Nonordinary VAD in user address-space teardown", __FILE__, __LINE__);
    }
    for (VirtualAddress page = vad->start; page < vad->end; page += g_page_size) {
      if (const PhysicalAddress phys = HAL_VMM_GetPhysicalAddress(page)) {
        HAL_VMM_UnmapPage(page);
        HAL_SMP_ShootdownTlb(page);
        if (as.owner_pid) HAL_PMM_FreeOwnedFrames(as.owner_pid, phys, 1);
        else
          HAL_PMM_FreeFrames(phys, 1);
      }
    }
    // Explicitly run the destructor to release the VAD's KObjectPtr<KMemoryBackingObject>
    // reference before returning the raw storage to the allocator.
    vad->~VirtualAddressDescriptor();
    KE_Free(vad);
  });

  HAL_VMM_SwitchAddressSpace(caller_root);
  HAL_CPU_RestoreInterrupts(irq_state);

  HAL_VMM_DestroyAddressSpace(as.translation_root);
  as.translation_root = PhysicalAddress(0);
  as.owner_pid = 0;
}

kstd::expected<void *, kstd::error_code> allocate_memory(AddressSpace &as, size_t size, ProtectFlags flags) noexcept {
  if (g_page_size == 0) return kstd::unexpected(KernelError::NotInitialized);
  if (!size) return kstd::unexpected(KernelError::InvalidSize);
  if (size > SIZE_MAX - (g_page_size - 1)) return kstd::unexpected(KernelError::ArithmeticOverflow);
  const size_t count = (size + g_page_size - 1) / g_page_size;
  if (count > SIZE_MAX / g_page_size) return kstd::unexpected(KernelError::ArithmeticOverflow);
  const PhysicalAddress phys = HAL_PMM_AllocateFrames(count);
  if (!phys) return kstd::unexpected(KernelError::OutOfMemory);

  // Use the AddressSpace's VAD tree to find a free region
  const ProtectFlags map_flags = (&as != &g_kernel_address_space) ? (flags | ProtectFlags::USER) : flags;
  auto region = KE_VMM_AllocateRegion(as, count * g_page_size, map_flags);
  if (!region) {
    HAL_PMM_FreeFrames(phys, count);
    return kstd::unexpected(region.error());
  }
  void *virt = *region;

  // HAL_VMM_MapPage always writes into whichever page directory is
  // currently active (CR3), not into an arbitrary AddressSpace, so the
  // target address space must be made active while the pages are mapped --
  // mirrors the pattern used by destroy_user_address_space. Without this,
  // callers that haven't already switched to `as` (e.g. the process's user
  // stack, mapped before its address space is switched in) would silently
  // map the pages into the caller's (kernel) address space instead.
  const PhysicalAddress caller_root = HAL_VMM_GetCurrentTranslationRoot();
  const bool switch_as = as.translation_root && as.translation_root != caller_root;
  uintptr_t irq_state = 0;
  if (switch_as) {
    irq_state = HAL_CPU_SaveAndDisableInterrupts();
    HAL_VMM_SwitchAddressSpace(as.translation_root);
  }

  bool ok = true;
  const VirtualAddress v_addr = VirtualAddress::from_ptr(virt);
  size_t mapped = 0;
  for (; mapped < count; ++mapped) {
    const auto page = v_addr + mapped * g_page_size;
    if (!HAL_VMM_MapPage(page, phys + mapped * g_page_size, map_flags | ProtectFlags::WRITE)) {
      ok = false;
      break;
    }
    HAL_SMP_ShootdownTlb(page);
    __builtin_memset(page.as_ptr(), 0, g_page_size);
    if (!has_flag(map_flags, ProtectFlags::WRITE)) {
      HAL_VMM_UnmapPage(page);
      HAL_SMP_ShootdownTlb(page);
      if (!HAL_VMM_MapPage(page, phys + mapped * g_page_size, map_flags)) {
        ok = false;
        break;
      }
    }
    HAL_SMP_ShootdownTlb(page);
  }
  if (!ok) {
    for (size_t i = 0; i < mapped; ++i) {
      HAL_VMM_UnmapPage(v_addr + i * g_page_size);
      HAL_SMP_ShootdownTlb(v_addr + i * g_page_size);
    }
    const auto cleanupStatus = KE_VMM_FreeRegion(as, virt);
    if (cleanupStatus) {
      KE_Print("vmm::allocate_memory: failed to release reserved region: {}",
               cleanupStatus.message());
    }
  }

  if (switch_as) {
    HAL_VMM_SwitchAddressSpace(caller_root);
    HAL_CPU_RestoreInterrupts(irq_state);
  }

  if (!ok) {
    HAL_PMM_FreeFrames(phys, count);
    return kstd::unexpected(KernelError::DeviceError);
  }

  return virt;
}

kstd::error_code map_fixed_memory(AddressSpace &as, uintptr_t address, size_t size, ProtectFlags flags) noexcept {
  if (g_page_size == 0) return KernelError::NotInitialized;
  if (!size) return KernelError::InvalidSize;
  if ((address & (g_page_size - 1)) || address < 0x10000 ||
      address >= static_cast<uintptr_t>(as.limit) ||
      size > static_cast<uintptr_t>(as.limit) - address) return KernelError::InvalidAddress;
  if (size > SIZE_MAX - (g_page_size - 1)) return KernelError::ArithmeticOverflow;
  const size_t aligned = (size + g_page_size - 1) & ~(g_page_size - 1);
  if (aligned > static_cast<uintptr_t>(as.limit) - address) return KernelError::OutOfRange;
  ExclusiveGuard guard(as.lock, is_user_as(as));
  bool overlap = false;
  as.vads.each([&](VirtualAddressDescriptor *existing) {
    if (address < static_cast<uintptr_t>(existing->end) &&
        address + aligned > static_cast<uintptr_t>(existing->start)) overlap = true;
  });
  if (overlap) return KernelError::ResourceConflict;

  auto vad = KE_CreateObject<VirtualAddressDescriptor>();
  if (!vad) return vad.error();
  const auto caller = HAL_VMM_GetCurrentTranslationRoot();
  const bool switch_as = as.translation_root != caller;
  uintptr_t irq_state = 0;
  if (switch_as) {
    irq_state = HAL_CPU_SaveAndDisableInterrupts();
    HAL_VMM_SwitchAddressSpace(as.translation_root);
  }
  const ProtectFlags map_flags = flags | ProtectFlags::USER;
  const ProtectFlags initialize_flags = map_flags | ProtectFlags::WRITE;
  size_t mapped = 0;
  kstd::error_code map_error = KernelError::OutOfMemory;
  for (; mapped < aligned; mapped += g_page_size) {
    const VirtualAddress page(address + mapped);
    if (HAL_VMM_GetPhysicalAddress(page)) {
      map_error = KernelError::ResourceConflict;
      break;
    }
    const auto phys = HAL_PMM_AllocateFrames(1);
    if (!phys) break;
    if (!HAL_VMM_MapPage(page, phys, initialize_flags)) {
      HAL_PMM_FreeFrames(phys, 1);
      map_error = KernelError::DeviceError;
      break;
    }
    HAL_SMP_ShootdownTlb(page);
    __builtin_memset(page.as_ptr(), 0, g_page_size);
    if (!has_flag(map_flags, ProtectFlags::WRITE)) {
      HAL_VMM_UnmapPage(page);
      HAL_SMP_ShootdownTlb(page);
      if (!HAL_VMM_MapPage(page, phys, map_flags)) {
        HAL_PMM_FreeFrames(phys, 1);
        map_error = KernelError::DeviceError;
        break;
      }
      HAL_SMP_ShootdownTlb(page);
    }
  }
  if (mapped != aligned) {
    for (size_t offset = 0; offset < mapped; offset += g_page_size) {
      const auto page = VirtualAddress(address + offset);
      const auto phys = HAL_VMM_GetPhysicalAddress(page);
      HAL_VMM_UnmapPage(page);
      HAL_SMP_ShootdownTlb(page);
      HAL_PMM_FreeFrames(phys, 1);
    }
  }
  if (switch_as) {
    HAL_VMM_SwitchAddressSpace(caller);
    HAL_CPU_RestoreInterrupts(irq_state);
  }
  if (mapped != aligned) return map_error;
  vad.value()->start = VirtualAddress(address);
  vad.value()->end = VirtualAddress(address + aligned);
  vad.value()->flags = map_flags;
  as.vads.insert(*vad->release());
  return {};
}
}// namespace kernel::vmm

UNDOS_KERNEL_API_DEF void *KE_Malloc(size_t size) noexcept {
  if (size == 0) [[unlikely]] {
    return nullptr;
  }

  for (auto &g_malloc_cache: g_malloc_caches) {
    if (size <= g_malloc_cache.object_size()) {
      auto allocate = g_malloc_cache.allocate();
      return allocate;
    }
  }

  // Larger than the biggest fixed-size cache: fall back to a dedicated,
  // page-backed allocation instead of failing outright.
  if (auto *large = kernel::memory::allocate_large(size)) {
    return large;
  }

  KE_Print("ALLOC FAILED: {}", size);
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
    slab->cache()->free(ptr, *slab);
    return;
  }

  kernel::memory::free_large(ptr);
}

namespace {
using kernel::vmm::VadKind;
using kernel::vmm::VirtualAddressDescriptor;

// BorrowedFree alias ranges of the kernel address space (the only one that
// holds borrowed aliases), ordered by start. Guarded by the arena mutex, like
// the VAD tree; reserve and release search it instead of walking every VAD.
kernel::adt::AvlTree<VirtualAddressDescriptor, &VirtualAddressDescriptor::free_node> g_borrowed_free;

void destroy_vads(VirtualAddressDescriptor *retired) noexcept {
  while (retired) {
    auto *next = retired->retired_next;
    retired->~VirtualAddressDescriptor();
    KE_Free(retired);
    retired = next;
  }
}

// Reserve a bare VAD; borrowed aliases first reuse unmapped alias ranges.
// Other reservations, or cache misses, advance the page-aligned bump pointer.
// Callers are responsible for setting vad->backing (if any).
kstd::expected<kernel::vmm::VirtualAddressDescriptor *, kstd::error_code>
reserve_vad(kernel::vmm::AddressSpace &as, size_t size, kernel::vmm::ProtectFlags flags,
            bool borrowed = false) noexcept {
  if (as.current_base == 0) {
    return kstd::unexpected(kernel::KernelError::NotInitialized);
  }
  if (size == 0) return kstd::unexpected(kernel::KernelError::InvalidSize);
  if (size > SIZE_MAX - (g_page_size - 1)) return kstd::unexpected(kernel::KernelError::ArithmeticOverflow);

  const size_t aligned_size = (size + g_page_size - 1) & ~(g_page_size - 1);

  if (!borrowed &&
      (as.current_base > as.limit || aligned_size > as.limit.value - as.current_base.value)) {
    return kstd::unexpected(kernel::KernelError::AddressSpaceExhausted);
  }

  if (auto vad = KE_CreateObject<kernel::vmm::VirtualAddressDescriptor>()) {
    kernel::ExclusiveGuard guard(as.lock, is_user_as(as));
    kstd::unique_lock arena(kernel::memory::arena_mutex(), kstd::defer_lock);
    if (&as == &g_kernel_address_space) arena.lock();
    kernel::VirtualAddress addr{};
    if (borrowed) {
      VirtualAddressDescriptor *free = nullptr;
      g_borrowed_free.each([&](VirtualAddressDescriptor *range) {
        if (!free &&
            range->end.value - range->start.value >= aligned_size &&
            range->start <= as.limit &&
            aligned_size <= as.limit.value - range->start.value)
          free = range;
      });
      if (free) {
        addr = free->start;
        g_borrowed_free.remove(free);
        if (free->end.value - addr.value == aligned_size) {
          free->kind = VadKind::BorrowedPreparing;
          // Return the unused preallocated descriptor outside the arena.
          arena.unlock();
          return free;
        }
        as.vads.remove(free);
        free->start += aligned_size;
        as.vads.insert(*free);
        g_borrowed_free.insert(*free);
      }
    }
    if (!addr) {
      // Metadata allocation can grow the slab heap and advance this address space.
      if (as.current_base > as.limit || aligned_size > as.limit.value - as.current_base.value) {
        return kstd::unexpected(kernel::KernelError::AddressSpaceExhausted);
      }
      addr = as.current_base;
      as.current_base += aligned_size;
    }
    vad.value()->start = addr;
    vad.value()->end = addr + size;
    vad.value()->flags = flags;
    vad.value()->kind = borrowed ? VadKind::BorrowedPreparing : VadKind::Ordinary;

    auto *rawvad = vad->release();
    as.vads.insert(*rawvad);
    return rawvad;
  } else {
    return kstd::unexpected(vad.error());
  }
}

// Called only for a descriptor exclusively owned by preparation or release.
kstd::error_code release_borrowed_region(VirtualAddressDescriptor *vad) noexcept {
  auto &as = g_kernel_address_space;
  bool empty = true;
  for (auto page = vad->start; page < vad->end; page += g_page_size) {
    if (HAL_VMM_GetPhysicalAddress(page)) empty = false;
  }
  VirtualAddressDescriptor *retired = nullptr;
  bool valid = false;
  {
    kstd::unique_lock arena(kernel::memory::arena_mutex());
    valid = as.vads.find(vad->start) == vad &&
            (vad->kind == VadKind::BorrowedUnmapping ||
             vad->kind == VadKind::BorrowedPreparing);
    if (!empty || !valid) {
      if (as.vads.find(vad->start) == vad) vad->kind = VadKind::BorrowedQuarantined;
    } else {
      vad->kind = VadKind::BorrowedFree;
      vad->borrowed_pointer = kernel::VirtualAddress(0);
      vad->borrowed_size = 0;
      vad->borrowed_source = nullptr;
      vad->borrowed_root = kernel::PhysicalAddress(0);
      vad->borrowed_asid = 0;
      // VADs never overlap, so the free neighbours are the descriptors that
      // contain end and start-1: two ordered lookups instead of a full walk.
      const auto free_at = [&](kernel::VirtualAddress addr) -> VirtualAddressDescriptor * {
        auto *other = as.vads.find(addr);
        return other && other != vad && other->kind == VadKind::BorrowedFree ? other : nullptr;
      };
      auto *right = free_at(vad->end);
      if (right && right->start != vad->end) right = nullptr;
      auto *left = vad->start.value ? free_at(kernel::VirtualAddress(vad->start.value - 1)) : nullptr;
      if (left && left->end != vad->start) left = nullptr;
      if (left || right) as.vads.remove(vad);
      VirtualAddressDescriptor *const neighbors[] = {left, right};
      for (auto *neighbor: neighbors) {
        if (!neighbor) continue;
        as.vads.remove(neighbor);
        g_borrowed_free.remove(neighbor);
        if (neighbor->start < vad->start) vad->start = neighbor->start;
        if (neighbor->end > vad->end) vad->end = neighbor->end;
        neighbor->retired_next = retired;
        retired = neighbor;
      }
      if (left || right) as.vads.insert(*vad);
      g_borrowed_free.insert(*vad);
    }
  }
  destroy_vads(retired);
  if (!empty || !valid) {
    KE_Print("VMM borrowed cleanup invariant: extent quarantined; live PTE={} claim={}",
             !empty, valid);
    return empty ? kernel::KernelError::InvalidState : kernel::KernelError::DeviceError;
  }
  return {};
}
}// namespace

UNDOS_KERNEL_API_DEF kstd::expected<void *, kstd::error_code>
KE_VMM_AllocateRegion(kernel::vmm::AddressSpace &as, size_t size, kernel::vmm::ProtectFlags flags) noexcept {
  auto vad = reserve_vad(as, size, flags);
  if (!vad) return kstd::unexpected(vad.error());
  return (*vad)->start.as_ptr();
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::vmm::VirtualAddressDescriptor *, kstd::error_code>
KE_VMM_MapBackingObject(kernel::vmm::AddressSpace &as, const kernel::KObjectPtr<kernel::KMemoryBackingObject> &backing, size_t size, kernel::vmm::ProtectFlags flags) noexcept {
  if (!backing) return kstd::unexpected(kernel::KernelError::InvalidArgument);
  auto vad = reserve_vad(as, size, flags);
  if (!vad) return kstd::unexpected(vad.error());
  (*vad)->backing = backing;
  return *vad;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_VMM_FreeRegion(kernel::vmm::AddressSpace &as, void *addr) noexcept {
  kernel::vmm::VirtualAddressDescriptor *vad = nullptr;
  {
    kernel::ExclusiveGuard guard(as.lock, is_user_as(as));
    kstd::unique_lock arena(kernel::memory::arena_mutex(), kstd::defer_lock);
    if (&as == &g_kernel_address_space) arena.lock();
    vad = as.vads.find(kernel::VirtualAddress::from_ptr(addr));
    if (vad && vad->kind != VadKind::Ordinary) return kernel::KernelError::InvalidState;
    if (vad) as.vads.remove(vad);
  }
  if (vad) {
    // Explicitly run the destructor to release the VAD's KObjectPtr<KMemoryBackingObject>
    // reference before returning the raw storage to the allocator.
    vad->~VirtualAddressDescriptor();
    KE_Free(vad);
    return {};
  }
  return kernel::KernelError::NotFound;
}

namespace {
// Creates and registers a fresh anonymous BackingObject sized to `size`
// under \Memory, giving each segment a uniquely named, inspectable object.
kstd::expected<kernel::KObjectPtr<kernel::KMemoryBackingObject>, kstd::error_code>
create_anonymous_backing(size_t size, const char *prefix) noexcept {
  static uint32_t next_id = 0;
  kstd::static_string<64> name;
  kstd::format(name, "{}{}", prefix, next_id++);

  auto backing = kernel::CreateKObject<kernel::KMemoryBackingObject>(name);
  if (!backing) return kstd::unexpected(backing.error());

  backing.value()->kind = kernel::KMemoryBackingObject::Kind::Anonymous;
  backing.value()->size = size;
  const auto status = KE_OB_InsertObject(KE_OB_GetMemoryDirectory(), *backing);
  if (status) return kstd::unexpected(status);
  return *backing;
}

kstd::expected<void *, kstd::error_code> allocate_user_backed_region(
    kernel::vmm::AddressSpace &as, size_t size, const char *name,
    kernel::vmm::ProtectFlags flags, const char *operation) noexcept {
  const auto backing = create_anonymous_backing(size, name);
  if (!backing) return kstd::unexpected(backing.error());

  auto vad = KE_VMM_MapBackingObject(as, *backing, size, flags);
  if (!vad) {
    if (const auto cleanupStatus = KE_OB_RemoveObject(KE_OB_GetMemoryDirectory(), *backing)) {
      KE_Print("{}: failed to remove backing object: {}", operation, cleanupStatus.message());
    }
    return kstd::unexpected(vad.error());
  }
  return (*vad)->start.as_ptr();
}
}// namespace

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KMemoryBackingObject>, kstd::error_code>
KE_VMM_CreateDirectBacking(kernel::VirtualAddress directAddress, size_t size) noexcept {
  static uint32_t next_id = 0;
  kstd::static_string<64> name;
  kstd::format(name, "DirectBacking{}", next_id++);

  auto backing = kernel::CreateKObject<kernel::KMemoryBackingObject>(name);
  if (!backing) return kstd::unexpected(backing.error());

  backing.value()->kind = kernel::KMemoryBackingObject::Kind::Anonymous;
  backing.value()->size = size;
  backing.value()->directAddress = directAddress;
  if (const auto status = KE_OB_InsertObject(KE_OB_GetMemoryDirectory(), *backing))
    return kstd::unexpected(status);
  return *backing;
}

UNDOS_KERNEL_API_DEF kstd::expected<void *, kstd::error_code> KE_VMM_AllocateUserData(kernel::vmm::AddressSpace &as, size_t size) noexcept {
  return allocate_user_backed_region(as, size, "UserData",
                                     kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE | kernel::vmm::ProtectFlags::USER,
                                     "KE_VMM_AllocateUserData");
}

UNDOS_KERNEL_API_DEF kstd::expected<void *, kstd::error_code> KE_VMM_AllocateUserProcess(kernel::vmm::AddressSpace &as, size_t size) noexcept {
  return allocate_user_backed_region(as, size, "UserProcess",
                                     kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::EXECUTE | kernel::vmm::ProtectFlags::USER,
                                     "KE_VMM_AllocateUserProcess");
}

UNDOS_KERNEL_API_DEF kernel::vmm::AddressSpace &KE_VMM_GetKernelAddressSpace() noexcept {
  return g_kernel_address_space;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_VMM_MapPhysical(kernel::vmm::AddressSpace &as, kernel::VirtualAddress virt, kernel::PhysicalAddress phys, kernel::vmm::ProtectFlags flags) noexcept {
  if (!virt || !phys) return kernel::KernelError::InvalidAddress;
  if (as.translation_root != HAL_VMM_GetCurrentTranslationRoot()) return kernel::KernelError::Unsupported;
  return HAL_VMM_MapPage(virt, phys, flags) ? kernel::KernelError::Success : kernel::KernelError::DeviceError;
}

UNDOS_KERNEL_API_DEF kstd::expected<void *, kstd::error_code> KE_VMM_MapBorrowed(void *addr, size_t size, kernel::vmm::AddressSpace &as) noexcept {
  if (!addr || size == 0) {
    return kstd::unexpected(kernel::KernelError::InvalidArgument);
  }
  const kernel::VirtualAddress user_addr = kernel::VirtualAddress::from_ptr(addr);
  if (g_page_size == 0) return kstd::unexpected(kernel::KernelError::NotInitialized);
  if (size > UINTPTR_MAX - user_addr.value ||
      user_addr.value + size > UINTPTR_MAX - (g_page_size - 1) ||
      size > SIZE_MAX - (g_page_size - 1)) {
    return kstd::unexpected(kernel::KernelError::ArithmeticOverflow);
  }

  const kernel::VirtualAddress page_start = user_addr.align_down(g_page_size);
  const kernel::VirtualAddress page_end = (user_addr + size).align_up(g_page_size);
  const size_t page_count = (page_end.value - page_start.value) / g_page_size;
  const size_t offset = user_addr.value - page_start.value;
  if (page_count > SIZE_MAX / sizeof(kernel::PhysicalAddress)) {
    return kstd::unexpected(kernel::KernelError::ArithmeticOverflow);
  }

  auto *phys_pages = static_cast<kernel::PhysicalAddress *>(KE_Malloc(page_count * sizeof(kernel::PhysicalAddress)));
  if (!phys_pages) {
    return kstd::unexpected(kernel::KernelError::OutOfMemory);
  }

  // Walk the source address space's page tables while it's active to
  // capture the physical frames backing the borrowed range.
  bool ok = true;
  {
    // Shared: the source's page tables must not change while they are read.
    // TODO(SMP): the frames stay referenced by the alias after this; a
    // concurrent unmap of the source needs a pin/refcount on them.
    kernel::SharedGuard guard(as.lock, is_user_as(as));
    const kernel::PhysicalAddress caller_root = HAL_VMM_GetCurrentTranslationRoot();
    const bool switch_to_source = as.translation_root && as.translation_root != caller_root;
    uintptr_t irq_state = 0;
    if (switch_to_source) {
      irq_state = HAL_CPU_SaveAndDisableInterrupts();
      HAL_VMM_SwitchAddressSpace(as.translation_root);
    }

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
      HAL_CPU_RestoreInterrupts(irq_state);
    }
  }

  if (!ok) {
    KE_Free(phys_pages);
    return kstd::unexpected(kernel::KernelError::InvalidAddress);
  }

  kernel::vmm::AddressSpace &kernel_as = KE_VMM_GetKernelAddressSpace();
#if __STDC_HOSTED__
  if (kernel::vmm::test_hooks::before_borrowed_reserve)
    kernel::vmm::test_hooks::before_borrowed_reserve();
#endif
  auto kernel_region_result = reserve_vad(kernel_as, page_count * g_page_size,
                                          kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE, true);
  if (!kernel_region_result) {
    KE_Free(phys_pages);
    return kstd::unexpected(kernel_region_result.error());
  }
  const kernel::VirtualAddress kernel_start = (*kernel_region_result)->start;
  auto *borrowed_vad = *kernel_region_result;
  borrowed_vad->borrowed_pointer = kernel_start + offset;
  borrowed_vad->borrowed_size = size;
  borrowed_vad->borrowed_source = &as;
  borrowed_vad->borrowed_root = as.translation_root;
  borrowed_vad->borrowed_asid = as.asid;
#if __STDC_HOSTED__
  if (kernel::vmm::test_hooks::before_borrowed_map)
    kernel::vmm::test_hooks::before_borrowed_map(borrowed_vad);
#endif
  size_t mapped = 0;
  for (; mapped < page_count; ++mapped) {
    if (!HAL_VMM_MapPage(kernel_start + mapped * g_page_size, phys_pages[mapped], kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE)) {
      ok = false;
      break;
    }
    HAL_SMP_ShootdownTlb(kernel_start + mapped * g_page_size);
  }

  KE_Free(phys_pages);

  if (!ok) {
    for (size_t i = 0; i < mapped; ++i) {
      const auto page = kernel_start + i * g_page_size;
      HAL_VMM_UnmapPage(page);
      HAL_SMP_ShootdownTlb(page);
    }
    const auto cleanupStatus = release_borrowed_region(borrowed_vad);
    if (cleanupStatus) {
      KE_Print("KE_VMM_MapBorrowed: failed to release reserved region: {}",
               cleanupStatus.message());
    }
    return kstd::unexpected(cleanupStatus ? cleanupStatus : kstd::error_code(kernel::KernelError::DeviceError));
  }

  {
    kstd::unique_lock arena(kernel::memory::arena_mutex());
    borrowed_vad->kind = VadKind::BorrowedActive;
  }
  return (kernel_start + offset).as_ptr();
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_VMM_UnmapBorrowed(void *addr, size_t size, kernel::vmm::AddressSpace &as) noexcept {
  if (!addr || size == 0) {
    return kernel::KernelError::InvalidArgument;
  }
  const auto input = kernel::VirtualAddress::from_ptr(addr);
  if (g_page_size == 0) return kernel::KernelError::NotInitialized;
  if (size > UINTPTR_MAX - input.value || input.value + size > UINTPTR_MAX - (g_page_size - 1) ||
      size > SIZE_MAX - (g_page_size - 1)) {
    return kernel::KernelError::ArithmeticOverflow;
  }

  VirtualAddressDescriptor *vad;
  {
    kstd::unique_lock arena(kernel::memory::arena_mutex());
    vad = g_kernel_address_space.vads.find(input);
    if (!vad || vad->kind == VadKind::Ordinary ||
        vad->kind == VadKind::BorrowedFree) return kernel::KernelError::NotFound;
    if (vad->kind != VadKind::BorrowedActive) return kernel::KernelError::InvalidState;
    if (vad->borrowed_pointer != input || vad->borrowed_source != &as ||
        vad->borrowed_root != as.translation_root ||
        vad->borrowed_asid != as.asid) return kernel::KernelError::InvalidAddress;
    if (vad->borrowed_size != size) return kernel::KernelError::InvalidSize;
    if (vad->start != input.align_down(g_page_size) ||
        vad->end != (input + size).align_up(g_page_size) ||
        !vad->start.is_aligned(g_page_size) || !vad->end.is_aligned(g_page_size)) {
      vad->kind = VadKind::BorrowedQuarantined;
    } else {
      vad->kind = VadKind::BorrowedUnmapping;
    }
  }
  if (vad->kind == VadKind::BorrowedQuarantined) {
    KE_Print("VMM borrowed release invariant: invalid recorded extent; quarantined");
    return kernel::KernelError::InvalidState;
  }
#if __STDC_HOSTED__
  if (kernel::vmm::test_hooks::after_borrowed_claim)
    kernel::vmm::test_hooks::after_borrowed_claim();
#endif
  for (auto page = vad->start; page < vad->end; page += g_page_size) {
    HAL_VMM_UnmapPage(page);
    // Unmapping removes a translation other CPUs may still have cached, so
    // this one has to go out to all of them, not just the local TLB.
    HAL_SMP_ShootdownTlb(page);
  }

  return release_borrowed_region(vad);
}

namespace {
// The single per-page preparation step of a direct user copy, called with
// as.lock held shared. Today it only requires the page to be present and to
// grant the access in hardware, so the copy can never fault. Copy-on-write
// (make a shared page private and writable) and demand paging belong here
// later; changing the page tables needs the lock exclusively, so that code
// must report "retry exclusive" and copy_user re-takes the lock, re-checks
// the page and fixes it. Callers do not change.
kstd::error_code prepare_user_page(kernel::vmm::AddressSpace &as, kernel::VirtualAddress page, bool write) noexcept {
  (void) as;
  auto present = kernel::vmm::ProtectFlags::NONE;
  if (!HAL_VMM_QueryPage(page, &present)) return kernel::KernelError::InvalidAddress;
  if (!kernel::vmm::has_flag(present, kernel::vmm::ProtectFlags::USER)) return kernel::KernelError::InvalidAddress;
  if (write && !kernel::vmm::has_flag(present, kernel::vmm::ProtectFlags::WRITE)) return kernel::KernelError::InvalidAddress;
  return {};
}

// Portable permission check against the kernel's own record of the range.
// Pages no VAD describes are judged by their page entry alone (above).
kstd::error_code check_user_page(kernel::vmm::AddressSpace &as, kernel::VirtualAddress page, bool write) noexcept {
  if (const auto *vad = as.vads.find(page)) {
    if (vad->kind != VadKind::Ordinary) return kernel::KernelError::InvalidAddress;
    const auto need = kernel::vmm::ProtectFlags::USER |
                      (write ? kernel::vmm::ProtectFlags::WRITE : kernel::vmm::ProtectFlags::READ);
    if (!kernel::vmm::has_flag(vad->flags, need)) return kernel::KernelError::InvalidAddress;
  }
  return prepare_user_page(as, page, write);
}

kstd::error_code copy_user(kernel::vmm::AddressSpace &as, uintptr_t user, uint8_t *kernel_ptr,
                           size_t size, bool write) noexcept {
  if (size == 0) return {};
  if (!user) return kernel::KernelError::InvalidAddress;
  if (g_page_size == 0) return kernel::KernelError::NotInitialized;
  if (size > UINTPTR_MAX - user) return kernel::KernelError::ArithmeticOverflow;
  if (as.limit && user + size > as.limit.value) return kernel::KernelError::InvalidAddress;

  if (!as.translation_root || as.translation_root != HAL_VMM_GetCurrentTranslationRoot()) {
    // Not the active address space: its pages are not visible here, so go
    // through a temporary borrowed alias instead.
    const auto mapped = KE_VMM_MapBorrowed(reinterpret_cast<void *>(user), size, as);
    if (!mapped) return mapped.error();
    if (write) __builtin_memcpy(*mapped, kernel_ptr, size);
    else __builtin_memcpy(kernel_ptr, *mapped, size);
    return KE_VMM_UnmapBorrowed(*mapped, size, as);
  }

  // The shared lock keeps every VAD and page entry of `as` in place for the
  // whole copy, on any CPU, so a checked page cannot be unmapped before it is
  // copied. Interrupts stay enabled.
  kernel::SharedGuard guard(as.lock, is_user_as(as));
  while (size) {
    const kernel::VirtualAddress page = kernel::VirtualAddress(user).align_down(g_page_size);
    const size_t room = page.value + g_page_size - user;
    const size_t chunk = size < room ? size : room;
    if (const auto status = check_user_page(as, page, write)) return status;
    if (write) __builtin_memcpy(reinterpret_cast<void *>(user), kernel_ptr, chunk);
    else __builtin_memcpy(kernel_ptr, reinterpret_cast<const void *>(user), chunk);
    user += chunk;
    kernel_ptr += chunk;
    size -= chunk;
  }
  return {};
}
}// namespace

UNDOS_KERNEL_API_DEF kstd::error_code KE_VMM_CopyFromUser(kernel::vmm::AddressSpace &as, void *dst, const void *userSrc, size_t size) noexcept {
  if (!dst && size) return kernel::KernelError::InvalidArgument;
  return copy_user(as, reinterpret_cast<uintptr_t>(userSrc), static_cast<uint8_t *>(dst), size, false);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_VMM_CopyToUser(kernel::vmm::AddressSpace &as, void *userDst, const void *src, size_t size) noexcept {
  if (!src && size) return kernel::KernelError::InvalidArgument;
  return copy_user(as, reinterpret_cast<uintptr_t>(userDst), const_cast<uint8_t *>(static_cast<const uint8_t *>(src)), size, true);
}

UNDOS_KERNEL_API_DEF void KE_VMM_HandlePagefault(const kernel::PageFaultInfo &info) noexcept {
  // Generic, architecture-agnostic page-fault dispatcher. The HAL has
  // already decoded the fault registers (faulting address, error code,
  // privilege level, faulting IP) into `info`; this is the single place
  // where all platforms funnel into for demand paging / copy-on-write /
  // pagefile handling.
  const auto fatal = [&](const char *message) {
    HAL_CPU_DisableInterrupts();
    KE_Print(
        "Page Fault: addr={} ip={} present={} write={} user={} exec={}",
        info.fault_addr.as_ptr<>(), info.ip.as_ptr<>(),
        info.present, info.write, info.user, info.exec);
    HAL_PLATFORM_Panic(message, __FILE__, __LINE__);
  };

  // 1. Identify the address space the fault occurred in: a user-mode fault
  // belongs to the currently running thread's owning process, while a
  // kernel-mode fault (or a user fault with no owning process, which should
  // never happen) is resolved against the kernel address space.
  kernel::vmm::AddressSpace *as = &KE_VMM_GetKernelAddressSpace();
  if (info.user) {
    if (const auto current_thread = KE_SCHED_GetCurrentThread(); current_thread && current_thread->owner_process) {
      as = &current_thread->owner_process->address_space;
    } else {
      fatal("Page Fault: user-mode fault with no owning process");
    }
  }

  // Shared for the user address space: its VADs must stay put while the
  // fault is resolved. Safe because no code faults while holding the lock
  // exclusively. Kernel-mode faults on the kernel space take no lock.
  // TODO(SMP): two CPUs faulting on one page may both map it; re-check the
  // PTE under an exclusive lock once demand paging is used for user memory.
  // A bad access from user mode is the program's bug, not the kernel's: end
  // that process instead of halting the system. The VAD lock is released
  // first, because terminating tears down the address space.
  if (info.user) {
    const char *invalid = nullptr;
    {
      kernel::SharedGuard check(as->lock, true);
      const auto *v = as->vads.find(info.fault_addr);
      if (!v) invalid = "access to unmapped/invalid address";
      else if (v->kind != kernel::vmm::VadKind::Ordinary) invalid = "access to reserved borrowed alias";
      else if (info.write && !kernel::vmm::has_flag(v->flags, kernel::vmm::ProtectFlags::WRITE)) invalid = "write to non-writable region";
      else if (info.exec && !kernel::vmm::has_flag(v->flags, kernel::vmm::ProtectFlags::EXECUTE)) invalid = "exec of non-executable region";
      else if (!kernel::vmm::has_flag(v->flags, kernel::vmm::ProtectFlags::USER)) invalid = "access to kernel-only region";
    }
    if (invalid) {
      KE_Print("Page Fault: {} addr={} ip={} write={} exec={}; terminating process", invalid,
               info.fault_addr.as_ptr<>(), info.ip.as_ptr<>(), info.write, info.exec);
      // The top stack words usually hold the caller's return address, which
      // names the code that passed the bad pointer to a leaf like strcat.
      uint32_t words[8] = {};
      if (!KE_VMM_CopyFromUser(*as, words, info.sp.as_ptr<>(), sizeof words))
        KE_Print("Page Fault: sp={} stack {} {} {} {} {} {} {} {}", info.sp.as_ptr<>(),
                 reinterpret_cast<void *>(words[0]), reinterpret_cast<void *>(words[1]),
                 reinterpret_cast<void *>(words[2]), reinterpret_cast<void *>(words[3]),
                 reinterpret_cast<void *>(words[4]), reinterpret_cast<void *>(words[5]),
                 reinterpret_cast<void *>(words[6]), reinterpret_cast<void *>(words[7]));
      kernel::process::terminate_current(-1);
    }
  }

  kernel::SharedGuard guard(as->lock, info.user);

  // 2. Look up the VAD covering the faulting address in that address space.
  // No VAD means the access targets memory that was never reserved for this
  // address space -- an invalid access that must be fatal.
  const auto *vad = as->vads.find(info.fault_addr);
  if (!vad) {
    fatal("Page Fault: access to unmapped/invalid address");
  }
  if (vad->kind != kernel::vmm::VadKind::Ordinary) {
    fatal("Page Fault: access to reserved borrowed alias");
  }

  // 3. Protection checks: reject accesses that violate the VAD's declared
  // permissions before doing any mapping work. These are fatal, since they
  // indicate either a user-mode bug (illegal access) or a kernel bug
  // (accessing user memory incorrectly).
  if (info.write && !kernel::vmm::has_flag(vad->flags, kernel::vmm::ProtectFlags::WRITE)) {
    fatal("Page Fault: write fault on non-writable region");
  }
  if (info.exec && !kernel::vmm::has_flag(vad->flags, kernel::vmm::ProtectFlags::EXECUTE)) {
    fatal("Page Fault: exec fault on non-executable region");
  }
  if (info.user && !kernel::vmm::has_flag(vad->flags, kernel::vmm::ProtectFlags::USER)) {
    fatal("Page Fault: user-mode fault on kernel-only region");
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
        fatal("Page Fault: out of physical memory");
      }
      if (const auto page_base = info.fault_addr.align_down(g_page_size);
          !HAL_VMM_MapPage(page_base, phys, vad->flags)) {
        fatal("Page Fault: HAL_VMM_MapPage failed");
      }
      break;
    }
    case kernel::KMemoryBackingObject::Kind::File:
      // TODO: read the page's contents from backing->file at
      // backing->file_offset + page_index * PAGE_SIZE once VFS read
      // integration is wired up here. For now, this is just a stub.
      fatal("Page Fault: file-backed read not implemented");
      break;
    case kernel::KMemoryBackingObject::Kind::Shared:
      // TODO: shared-memory semantics (mirrors Anonymous/File depending on
      // design) are not implemented yet.
      fatal("Page Fault: shared-memory fault not implemented");
      break;
    case kernel::KMemoryBackingObject::Kind::KernelModule:
      // TODO: file-backed kernel module page-in not implemented yet.
      fatal("Page Fault: kernel-module-backed fault not implemented");
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
