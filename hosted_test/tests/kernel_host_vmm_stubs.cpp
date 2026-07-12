// Host stand-ins for the HAL PMM/VMM entry points used by kernel/src/vmm.cpp
// (and, transitively, kernel/src/memory/*.cpp). On real hardware these walk
// page tables and physical frame bitmaps; on the host there is no paging, so
// instead we back everything with a single 32MB malloc'd buffer that acts as
// "fake RAM". The mapping is identity-like: HAL_VMM_GetHighestVirtualAddress
// anchors the kernel heap to the base of that buffer, so the "virtual
// addresses" the SLAB allocator computes arithmetically are real, dereferenceable
// host pointers into the buffer. "Physical addresses" are just opaque bump-allocated
// handles (never dereferenced by vmm.cpp), tracked alongside a virt->phys table so
// HAL_VMM_GetPhysicalAddress/HAL_VMM_UnmapPage stay consistent.

#include <kernel/hal_interface.hpp>

#include <cstdlib>
#include <unordered_map>

uint32_t g_page_size = 4096;

namespace {
constexpr size_t kFakeRamSize = 32u * 1024 * 1024;// 32MB fake RAM

uint8_t *g_fake_ram = nullptr;
uintptr_t g_next_physical = 0x1000;// arbitrary, never dereferenced
std::unordered_map<uintptr_t, kernel::PhysicalAddress> g_page_table;

uint8_t *FakeRamBase() {
  if (!g_fake_ram) {
    g_fake_ram = static_cast<uint8_t *>(std::aligned_alloc(g_page_size, kFakeRamSize));
  }
  return g_fake_ram;
}
}// namespace

UNDOS_HAL_API_DEF kernel::VirtualAddress HAL_VMM_GetHighestVirtualAddress() noexcept {
  // vmm::init() computes heap_start as align_down(this + page_size - 1, page_size) + page_size;
  // since FakeRamBase() is already page-aligned, that resolves back to FakeRamBase() exactly.
  return kernel::VirtualAddress::from_ptr(FakeRamBase()) - g_page_size;
}

UNDOS_HAL_API_DEF void HAL_VMM_FinalizeInit() noexcept {}

UNDOS_HAL_API_DEF kernel::PhysicalAddress HAL_VMM_GetCurrentTranslationRoot() noexcept {
  return kernel::PhysicalAddress(1);
}

UNDOS_HAL_API_DEF kernel::PhysicalAddress HAL_PMM_AllocateFrames(size_t count) noexcept {
  const auto addr = g_next_physical;
  g_next_physical += count * g_page_size;
  return kernel::PhysicalAddress(addr);
}

UNDOS_HAL_API_DEF kernel::PhysicalAddress HAL_PMM_AllocateFramesDMA(size_t count) noexcept {
  return HAL_PMM_AllocateFrames(count);
}

UNDOS_HAL_API_DEF void HAL_PMM_FreeFrames(kernel::PhysicalAddress base, size_t count) noexcept {
  (void) base;
  (void) count;
}

UNDOS_HAL_API_DEF void HAL_PMM_ReserveRegion(kernel::PhysicalAddress base, size_t length) noexcept {
  (void) base;
  (void) length;
}

UNDOS_HAL_API_DEF bool HAL_VMM_MapPage(kernel::VirtualAddress virt, kernel::PhysicalAddress phys, kernel::vmm::ProtectFlags flags) noexcept {
  (void) flags;
  g_page_table[virt.value] = phys;
  return true;
}

UNDOS_HAL_API_DEF void HAL_VMM_UnmapPage(kernel::VirtualAddress virt) noexcept {
  g_page_table.erase(virt.value);
}

UNDOS_HAL_API_DEF kernel::PhysicalAddress HAL_VMM_GetPhysicalAddress(kernel::VirtualAddress virt) noexcept {
  const auto it = g_page_table.find(virt.value);
  return it != g_page_table.end() ? it->second : kernel::PhysicalAddress(0);
}

UNDOS_HAL_API_DEF void HAL_VMM_SwitchAddressSpace(kernel::PhysicalAddress new_root) noexcept {
  (void) new_root;
}

UNDOS_HAL_API_DEF kernel::PhysicalAddress HAL_VMM_CreateAddressSpace() noexcept {
  return kernel::PhysicalAddress(2);
}

UNDOS_HAL_API_DEF void HAL_VMM_DestroyAddressSpace(kernel::PhysicalAddress root) noexcept {
  (void) root;
}

UNDOS_HAL_API_DEF void HAL_VMM_Flush(kernel::VirtualAddress virt) noexcept {
  (void) virt;
}

UNDOS_HAL_API_DEF void HAL_CPU_DisableInterrupts() noexcept {}
UNDOS_HAL_API_DEF void HAL_CPU_EnableInterrupts() noexcept {}
