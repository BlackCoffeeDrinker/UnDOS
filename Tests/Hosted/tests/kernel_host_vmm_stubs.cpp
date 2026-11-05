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

#include "scheduler/CpuContext.hpp"
#include "task_manager_scheduler_hal_hooks.hpp"

#include <array>
#include <cstdlib>
#include <unordered_map>

uint32_t g_page_size = 4096;

namespace {
constexpr size_t kFakeRamSize = 32u * 1024 * 1024;// 32MB fake RAM

uint8_t *g_fake_ram = nullptr;
uintptr_t g_next_physical = 0x1000;// arbitrary, never dereferenced
std::unordered_map<uintptr_t, kernel::PhysicalAddress> g_page_table;
std::unordered_map<uintptr_t, kernel::vmm::ProtectFlags> g_page_flags;
std::unordered_map<uint32_t, uint64_t> g_owner_frames;
kernel::PhysicalMemoryUsage g_pmm_usage{};
bool g_pmm_ready = false;
size_t g_map_calls = 0;
bool g_fail_next_map = false;
size_t g_maps_until_failure = SIZE_MAX;
uintptr_t g_ignore_unmap = 0;
size_t g_unmap_calls = 0;
std::array<uintptr_t, 8192> g_shootdowns{};
size_t g_shootdown_count = 0;
kernel::PhysicalAddress g_scheduler_root(1);
kernel::VirtualAddress g_scheduler_stack{};
kernel::VirtualAddress g_scheduler_saved_stack{};
std::array<kernel::VirtualAddress, 16> g_scheduler_switch_from{};
std::array<kernel::VirtualAddress, 16> g_scheduler_switch_to{};
size_t g_scheduler_switch_count = 0;
uint32_t g_host_irq_depth = 0;
std::unordered_map<const kernel::VirtualAddress *, uint32_t> g_context_irq_depths;
std::unordered_map<uintptr_t, uint32_t> g_initialized_irq_depths;

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

// On real hardware, HAL_PLATFORM_Init() (ia32.cpp) seeds the kernel address
// space's current_base/limit from boot_info.mapped_memory before anything
// else runs. Hosted tests never call HAL_PLATFORM_Init, so test support code
// (ob_test_support.hpp) calls this instead to seed the kernel AddressSpace
// with the bounds of the fake RAM buffer.
kernel::VirtualAddress HAL_TEST_GetFakeRamBase() noexcept {
  return kernel::VirtualAddress::from_ptr(FakeRamBase());
}

size_t HAL_TEST_GetFakeRamSize() noexcept {
  return kFakeRamSize;
}

namespace test_sched_hal {
void Reset() noexcept {
  g_scheduler_root = kernel::PhysicalAddress(1);
  g_scheduler_stack = kernel::VirtualAddress(0);
  g_scheduler_saved_stack = kernel::VirtualAddress(0);
  g_scheduler_switch_count = 0;
  g_scheduler_switch_from = {};
  g_scheduler_switch_to = {};
  g_host_irq_depth = 0;
  g_context_irq_depths.clear();
  g_initialized_irq_depths.clear();
}

void SetRoot(kernel::PhysicalAddress root) noexcept { g_scheduler_root = root; }
void SetStack(kernel::VirtualAddress stack) noexcept { g_scheduler_stack = stack; }
void SetSavedStack(kernel::VirtualAddress stack) noexcept {
  g_scheduler_saved_stack = stack;
}
void SetHardwareInterruptDepth(uint32_t depth) noexcept {
  g_host_irq_depth = depth;
}
size_t SwitchCount() noexcept { return g_scheduler_switch_count; }

kernel::VirtualAddress SwitchFrom(size_t index) noexcept {
  return index < g_scheduler_switch_count ? g_scheduler_switch_from[index]
                                         : kernel::VirtualAddress{};
}

kernel::VirtualAddress SwitchTo(size_t index) noexcept {
  return index < g_scheduler_switch_count ? g_scheduler_switch_to[index]
                                         : kernel::VirtualAddress{};
}
}  // namespace test_sched_hal

namespace test_host_vmm {
void ResetMapCalls() noexcept { g_map_calls = 0; }
size_t MapCalls() noexcept { return g_map_calls; }
void FailNextMap() noexcept { g_fail_next_map = true; }
void FailMapAfter(size_t successful) noexcept { g_maps_until_failure = successful; }
void IgnoreUnmap(uintptr_t page) noexcept { g_ignore_unmap = page; }
size_t UnmapCalls() noexcept { return g_unmap_calls; }
void ResetShootdowns() noexcept { g_shootdown_count = 0; }
size_t ShootdownCount() noexcept { return g_shootdown_count; }
uintptr_t ShootdownAt(size_t index) noexcept {
  return index < g_shootdown_count && index < g_shootdowns.size() ? g_shootdowns[index] : 0;
}
}

namespace test_host_pmm {
void SetUsage(const kernel::PhysicalMemoryUsage &usage) noexcept {
  g_pmm_usage = usage;
  g_pmm_ready = usage.initialized;
}
void SetOwnerFrames(uint32_t pid, uint64_t frames) noexcept {
  g_owner_frames[pid] = frames;
}
void Reset() noexcept {
  g_owner_frames.clear();
  g_pmm_usage = {};
  g_pmm_ready = false;
}
}

UNDOS_HAL_API_DEF void HAL_VMM_FinalizeInit() noexcept {}

UNDOS_HAL_API_DEF kernel::PhysicalAddress HAL_VMM_GetCurrentTranslationRoot() noexcept {
  return g_scheduler_root;
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

UNDOS_HAL_API_DEF bool HAL_PMM_QueryUsage(
    kernel::PhysicalMemoryUsage *out) noexcept {
  if (!out || !g_pmm_ready) return false;
  *out = g_pmm_usage;
  return true;
}

UNDOS_HAL_API_DEF bool HAL_PMM_QueryOwnerFrames(uint32_t pid,
                                                uint64_t *frames) noexcept {
  if (!frames) return false;
  const auto found = g_owner_frames.find(pid);
  if (found == g_owner_frames.end()) return false;
  *frames = found->second;
  return true;
}

UNDOS_HAL_API_DEF bool HAL_PMM_RegisterOwner(uint32_t pid) noexcept {
  return pid != 0;
}

UNDOS_HAL_API_DEF void HAL_PMM_UnregisterOwner(uint32_t pid) noexcept {
  (void) pid;
}

UNDOS_HAL_API_DEF bool HAL_PMM_FreeOwnedFrames(
    uint32_t pid, kernel::PhysicalAddress base, size_t count) noexcept {
  (void) base;
  (void) count;
  return pid != 0;
}

UNDOS_HAL_API_DEF void HAL_PMM_ReserveRegion(kernel::PhysicalAddress base, size_t length) noexcept {
  (void) base;
  (void) length;
}

UNDOS_HAL_API_DEF bool HAL_VMM_MapPage(kernel::VirtualAddress virt, kernel::PhysicalAddress phys, kernel::vmm::ProtectFlags flags) noexcept {
  ++g_map_calls;
  (void) flags;
  if (g_fail_next_map) { g_fail_next_map = false; return false; }
  if (g_maps_until_failure != SIZE_MAX) {
    if (!g_maps_until_failure) { g_maps_until_failure = SIZE_MAX; return false; }
    --g_maps_until_failure;
  }
  g_page_table[virt.value] = phys;
  g_page_flags[virt.value] = flags | kernel::vmm::ProtectFlags::READ;
  return true;
}

UNDOS_HAL_API_DEF bool HAL_VMM_QueryPage(kernel::VirtualAddress virt, kernel::vmm::ProtectFlags *flags) noexcept {
  const auto page = virt.value & ~(uintptr_t(g_page_size) - 1);
  if (!g_page_table.count(page)) return false;
  if (flags) {
    const auto found = g_page_flags.find(page);
    *flags = found == g_page_flags.end() ? kernel::vmm::ProtectFlags::READ : found->second;
  }
  return true;
}

UNDOS_HAL_API_DEF void HAL_VMM_UnmapPage(kernel::VirtualAddress virt) noexcept {
  ++g_unmap_calls;
  if (virt.value == g_ignore_unmap) return;
  g_page_table.erase(virt.value);
  g_page_flags.erase(virt.value);
}

UNDOS_HAL_API_DEF kernel::PhysicalAddress HAL_VMM_GetPhysicalAddress(kernel::VirtualAddress virt) noexcept {
  const auto it = g_page_table.find(virt.value);
  return it != g_page_table.end() ? it->second : kernel::PhysicalAddress(0);
}

UNDOS_HAL_API_DEF void HAL_VMM_SwitchAddressSpace(kernel::PhysicalAddress new_root) noexcept {
  g_scheduler_root = new_root;
}

UNDOS_HAL_API_DEF kernel::PhysicalAddress HAL_VMM_CreateAddressSpace() noexcept {
  return kernel::PhysicalAddress(2);
}

UNDOS_HAL_API_DEF bool HAL_VMM_SetAddressSpaceOwner(
    kernel::PhysicalAddress root, uint32_t pid) noexcept {
  return root && pid != 0;
}

UNDOS_HAL_API_DEF void HAL_VMM_DestroyAddressSpace(kernel::PhysicalAddress root) noexcept {
  (void) root;
}

UNDOS_HAL_API_DEF void HAL_VMM_Flush(kernel::VirtualAddress virt) noexcept {
  (void) virt;
}

// Simulated CPU identity / interrupt state. There is a single host thread
// running the tests, so a pair of plain globals is enough; tests drive them
// through smp_test_hooks.hpp.
namespace {
uint32_t g_host_cpu_id = 0;
bool g_host_interrupts_enabled = true;
}// namespace

namespace test_smp {
void SetCurrentCpuId(const uint32_t id) noexcept { g_host_cpu_id = id; }
void Reset() noexcept {
  g_host_cpu_id = 0;
  g_host_interrupts_enabled = true;
}
bool InterruptsEnabled() noexcept { return g_host_interrupts_enabled; }
void SetInterruptsEnabled(const bool enabled) noexcept { g_host_interrupts_enabled = enabled; }
}// namespace test_smp

UNDOS_HAL_API_DEF void HAL_CPU_DisableInterrupts() noexcept { g_host_interrupts_enabled = false; }
UNDOS_HAL_API_DEF void HAL_CPU_EnableInterrupts() noexcept { g_host_interrupts_enabled = true; }
UNDOS_HAL_API_DEF bool HAL_CPU_InHardwareInterrupt() noexcept {
  return g_host_irq_depth != 0;
}

UNDOS_HAL_API_DEF kernel::VirtualAddress HAL_CPU_InitThreadContext(
    kernel::VirtualAddress stack_top,
    kernel::cfunc<void()> trampoline) noexcept {
  (void) trampoline;
  const kernel::VirtualAddress stack_pointer(stack_top.value - 28);
  g_initialized_irq_depths[stack_pointer.value] = 0;
  return stack_pointer;
}

UNDOS_HAL_API_DEF kernel::VirtualAddress HAL_CPU_InitUserThreadContext(
    kernel::VirtualAddress kernel_stack_top,
    kernel::VirtualAddress user_entry,
    kernel::VirtualAddress user_stack_top) noexcept {
  (void) user_entry;
  (void) user_stack_top;
  const kernel::VirtualAddress stack_pointer(kernel_stack_top.value - 44);
  g_initialized_irq_depths[stack_pointer.value] = 0;
  return stack_pointer;
}

UNDOS_HAL_API_DEF uint32_t HAL_CPU_GetCurrentId() noexcept { return g_host_cpu_id; }

UNDOS_HAL_API_DEF uintptr_t HAL_CPU_SaveAndDisableInterrupts() noexcept {
  const uintptr_t state = g_host_interrupts_enabled ? 1u : 0u;
  g_host_interrupts_enabled = false;
  return state;
}

UNDOS_HAL_API_DEF void HAL_CPU_RestoreInterrupts(const uintptr_t state) noexcept {
  if (state != 0) {
    g_host_interrupts_enabled = true;
  }
}

UNDOS_HAL_API_DEF kernel::VirtualAddress HAL_CPU_GetKernelStack() noexcept {
  return g_scheduler_stack;
}

UNDOS_HAL_API_DEF void HAL_CPU_SetKernelStack(
    kernel::VirtualAddress stack) noexcept {
  g_scheduler_stack = stack;
}

UNDOS_HAL_API_DEF void HAL_CPU_SwitchContext(
    kernel::VirtualAddress &save_sp_out,
    kernel::VirtualAddress load_sp) noexcept {
  g_context_irq_depths[&save_sp_out] = g_host_irq_depth;
  const auto *incoming = kernel::cpu::current_context().current.get();
  if (incoming) {
    const auto found = g_context_irq_depths.find(&incoming->kernel_stack_top);
    if (found != g_context_irq_depths.end()) {
      g_host_irq_depth = found->second;
    } else {
      const auto initialized = g_initialized_irq_depths.find(load_sp.value);
      g_host_irq_depth = initialized == g_initialized_irq_depths.end()
          ? 0 : initialized->second;
      g_context_irq_depths[&incoming->kernel_stack_top] = g_host_irq_depth;
    }
  } else {
    g_host_irq_depth = 0;
  }
  if (g_scheduler_switch_count < g_scheduler_switch_from.size()) {
    g_scheduler_switch_from[g_scheduler_switch_count] = save_sp_out;
    g_scheduler_switch_to[g_scheduler_switch_count] = load_sp;
    ++g_scheduler_switch_count;
  }
  if (g_scheduler_saved_stack) save_sp_out = g_scheduler_saved_stack;
}

namespace hal::x86 {
void begin_hardware_interrupt() noexcept { ++g_host_irq_depth; }
void end_hardware_interrupt() noexcept {
  if (g_host_irq_depth) --g_host_irq_depth;
}
}// namespace hal::x86

UNDOS_HAL_API_DEF uint32_t HAL_PLATFORM_GetCpuCount() noexcept { return 1; }

UNDOS_HAL_API_DEF bool HAL_SMP_StartProcessor(uint32_t cpu_id,
                                              kernel::cfunc<void(uint32_t)> entry,
                                              kernel::VirtualAddress stack_top) noexcept {
  (void) cpu_id;
  (void) entry;
  (void) stack_top;
  return false;
}

UNDOS_HAL_API_DEF void HAL_SMP_SendRescheduleIpi(uint32_t cpu_id) noexcept { (void) cpu_id; }

UNDOS_HAL_API_DEF void HAL_SMP_ShootdownTlb(const kernel::VirtualAddress addr) noexcept {
  if (g_shootdown_count < g_shootdowns.size()) g_shootdowns[g_shootdown_count] = addr.value;
  ++g_shootdown_count;
  HAL_VMM_Flush(addr);
}
