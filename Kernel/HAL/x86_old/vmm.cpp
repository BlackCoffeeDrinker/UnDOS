
#include "vmm.hpp"
#include "pmm_accounting.hpp"

#include "pmm.hpp"
#include "v86.hpp"

#include <kernel/hal_interface.hpp>
#include <kernel/spinlock.hpp>

using kernel::PhysicalAddress;
using kernel::VirtualAddress;

namespace {
bool register_root(uintptr_t root) noexcept;
void unregister_root(uintptr_t root) noexcept;
void propagate_kernel_pde(size_t pd_index) noexcept;
}// namespace

namespace hal::x86 {
constexpr uintptr_t RECURSIVE_PT_WINDOW = 0xFFC00000;
constexpr uintptr_t RECURSIVE_PD_WINDOW = 0xFFFFF000;

static page_directory_entry_t *get_page_directory() noexcept {
  return reinterpret_cast<page_directory_entry_t *>(RECURSIVE_PD_WINDOW);
}

static page_table_entry_t *get_page_table(VirtualAddress virtual_addr) noexcept {
  const uintptr_t pd_index = static_cast<uintptr_t>(virtual_addr >> 22);
  return reinterpret_cast<page_table_entry_t *>(RECURSIVE_PT_WINDOW + (pd_index * 4096));
}

static page_table_entry_t *get_recursive_window(size_t slot) noexcept {
  return reinterpret_cast<page_table_entry_t *>(RECURSIVE_PT_WINDOW + (slot * 4096));
}

static PageFlags translate_flags(kernel::vmm::ProtectFlags flags) noexcept {
  // Pages mapped via map_page must be present by default
  auto x86_flags = PageFlags::PRESENT;

  if (has_flag(flags, kernel::vmm::ProtectFlags::WRITE)) {
    x86_flags = x86_flags | PageFlags::WRITABLE;
  }
  if (has_flag(flags, kernel::vmm::ProtectFlags::USER)) {
    x86_flags = x86_flags | PageFlags::USER;
  }
  if (has_flag(flags, kernel::vmm::ProtectFlags::NOCACHE)) {
    x86_flags = x86_flags | PageFlags::CACHE_DISABLE | PageFlags::WRITE_THROUGH;
  }

  return x86_flags;
}

void init_vmm() noexcept {
  // Read the active Page Directory established by Stage 1.5
  uintptr_t current_pd_phys;
  __asm__ volatile("mov %%cr3, %0" : "=r"(current_pd_phys));

  // Access the page directory structure directly using the recursive window.
  // Stage 1.5 must have pre-mapped this entry; we explicitly assert its validity
  // and re-apply permissions to ensure the HAL can modify any page table.
  auto *directory = get_page_directory();

  constexpr size_t recursive_slot = 1023;// 0xFFC00000 >> 22

  directory[recursive_slot].set_page_table(current_pd_phys, PageFlags::PRESENT | PageFlags::WRITABLE | PageFlags::USER);

  // Force a global TLB flush by reloading CR3 to validate our window setup immediately
  __asm__ volatile("mov %0, %%cr3" ::"r"(current_pd_phys) : "memory");

  if (!::register_root(current_pd_phys)) {
    HAL_PLATFORM_Panic("Failed to register the kernel address space", __FILE__, __LINE__);
  }
}
}// namespace hal::x86


UNDOS_HAL_API_DEF void HAL_VMM_FinalizeInit() noexcept {
  // The kernel has the virtual memory manager up and running

  // Registers/reserves any v86 monitor state that must exist before the
  // first HAL_x86_ExecBiosInt call (the vector-13 stub above is already
  // installed at this point).
  hal::x86::init_v86_monitor();
}

UNDOS_HAL_API_DEF PhysicalAddress HAL_VMM_GetCurrentTranslationRoot() noexcept {
  uintptr_t current_pd_phys;
  __asm__ volatile("mov %%cr3, %0" : "=r"(current_pd_phys));
  return current_pd_phys;
}

UNDOS_HAL_API_DEF PhysicalAddress HAL_VMM_GetPhysicalAddress(VirtualAddress virt) noexcept {
  const uintptr_t pd_index = static_cast<uintptr_t>(virt >> 22);
  const uintptr_t pt_index = static_cast<uintptr_t>((virt >> 12) & 0x3FF);
  const uintptr_t page_offset = static_cast<uintptr_t>(virt & 0xFFF);

  const auto *directory = hal::x86::get_page_directory();
  if (!directory[pd_index].is_present()) return 0;

  const auto *page_table = hal::x86::get_page_table(virt);
  if (!page_table[pt_index].is_present()) return 0;

  return page_table[pt_index].get_frame() + page_offset;
}

UNDOS_HAL_API_DEF bool HAL_VMM_QueryPage(VirtualAddress virt, kernel::vmm::ProtectFlags *flags) noexcept {
  using hal::x86::PageFlags;
  const uintptr_t pd_index = static_cast<uintptr_t>(virt >> 22);
  const uintptr_t pt_index = static_cast<uintptr_t>((virt >> 12) & 0x3FF);

  const auto *directory = hal::x86::get_page_directory();
  if (!directory[pd_index].is_present()) return false;
  const auto *page_table = hal::x86::get_page_table(virt);
  if (!page_table[pt_index].is_present()) return false;

  // Both levels must grant an access for the CPU to allow it at CPL3.
  const auto pde = static_cast<PageFlags>(directory[pd_index].value);
  const auto pte = static_cast<PageFlags>(page_table[pt_index].value);
  auto result = kernel::vmm::ProtectFlags::READ;
  if (has_flag(pde, PageFlags::WRITABLE) && has_flag(pte, PageFlags::WRITABLE))
    result = result | kernel::vmm::ProtectFlags::WRITE;
  if (has_flag(pde, PageFlags::USER) && has_flag(pte, PageFlags::USER))
    result = result | kernel::vmm::ProtectFlags::USER;
  if (flags) *flags = result;
  return true;
}

UNDOS_HAL_API_DEF void HAL_VMM_SwitchAddressSpace(PhysicalAddress new_root) noexcept {
  __asm__ volatile("mov %0, %%cr3" ::"r"(static_cast<uintptr_t>(new_root)) : "memory");
}

namespace {
// A page directory index in the kernel's higher-half that is guaranteed to be
// unused by any real mapping; borrowed briefly to peek at a not-yet-active
// page directory frame through the recursive window mechanism.
constexpr size_t TEMP_PD_SLOT = 1022;
constexpr size_t KERNEL_SPLIT = 768;// 0xC0000000 >> 22

// Every live page directory (the boot/kernel one plus each user address
// space). Kernel-half page tables are shared: when one is created, its PDE is
// copied into all of them so a kernel mapping made while any address space is
// active is visible from every other one.
constexpr size_t MAX_ADDRESS_SPACES = 64;
uintptr_t g_roots[MAX_ADDRESS_SPACES];
uint32_t g_root_owners[MAX_ADDRESS_SPACES];
size_t g_root_count = 0;
kernel::SpinLock g_root_lock;

bool register_root(uintptr_t root) noexcept {
  const uintptr_t state = g_root_lock.lock_irqsave();
  if (g_root_count >= MAX_ADDRESS_SPACES) {
    g_root_lock.unlock_irqrestore(state);
    return false;
  }
  g_roots[g_root_count] = root;
  g_root_owners[g_root_count] = 0;
  ++g_root_count;
  g_root_lock.unlock_irqrestore(state);
  return true;
}

void unregister_root(uintptr_t root) noexcept {
  const uintptr_t state = g_root_lock.lock_irqsave();
  for (size_t i = 0; i < g_root_count; ++i) {
    if (g_roots[i] == root) {
      --g_root_count;
      g_roots[i] = g_roots[g_root_count];
      g_root_owners[i] = g_root_owners[g_root_count];
      g_roots[g_root_count] = 0;
      g_root_owners[g_root_count] = 0;
      g_root_lock.unlock_irqrestore(state);
      return;
    }
  }
  g_root_lock.unlock_irqrestore(state);
}

uint32_t address_space_owner(uintptr_t root) noexcept {
  const uintptr_t state = g_root_lock.lock_irqsave();
  uint32_t pid = 0;
  for (size_t i = 0; i < g_root_count; ++i) {
    if (g_roots[i] == root) {
      pid = g_root_owners[i];
      break;
    }
  }
  g_root_lock.unlock_irqrestore(state);
  return pid;
}

// Copy PDE `pd_index` of the active directory into every other registered one.
void propagate_kernel_pde(size_t pd_index) noexcept {
  auto *directory = hal::x86::get_page_directory();
  const uintptr_t current = static_cast<uintptr_t>(HAL_VMM_GetCurrentTranslationRoot());
  uintptr_t roots[MAX_ADDRESS_SPACES]{};
  size_t count = 0;
  const uintptr_t state = g_root_lock.lock_irqsave();
  count = g_root_count;
  for (size_t i = 0; i < count; ++i) roots[i] = g_roots[i];
  g_root_lock.unlock_irqrestore(state);
  for (size_t i = 0; i < count; ++i) {
    if (roots[i] == current) continue;
    directory[TEMP_PD_SLOT].set_page_table(roots[i], hal::x86::PageFlags::PRESENT | hal::x86::PageFlags::WRITABLE);
    HAL_VMM_Flush(VirtualAddress(hal::x86::RECURSIVE_PT_WINDOW + (TEMP_PD_SLOT * 4096)));
    auto *window = reinterpret_cast<hal::x86::page_directory_entry_t *>(
        hal::x86::get_recursive_window(TEMP_PD_SLOT));
    window[pd_index] = directory[pd_index];
  }
  directory[TEMP_PD_SLOT].clear();
  HAL_VMM_Flush(VirtualAddress(hal::x86::RECURSIVE_PT_WINDOW + (TEMP_PD_SLOT * 4096)));
}
}// namespace

UNDOS_HAL_API_DEF PhysicalAddress HAL_VMM_CreateAddressSpace() noexcept {
  const PhysicalAddress new_pd_phys = HAL_PMM_AllocateFrames(1);
  if (!new_pd_phys) {
    return 0;
  }

  auto *directory = hal::x86::get_page_directory();
  // Temporarily alias the new page directory's frame as a page table so we can
  // read/write its 1024 entries through the recursive window.
  directory[TEMP_PD_SLOT].set_page_table(
      static_cast<uintptr_t>(new_pd_phys),
      hal::x86::PageFlags::PRESENT | hal::x86::PageFlags::WRITABLE);
  HAL_VMM_Flush(VirtualAddress(hal::x86::RECURSIVE_PT_WINDOW + (TEMP_PD_SLOT * 4096)));

  auto *new_pd = reinterpret_cast<hal::x86::page_directory_entry_t *>(
      hal::x86::get_recursive_window(TEMP_PD_SLOT));

  constexpr size_t kernel_split = 768;// 0xC0000000 >> 22

  // Empty user half
  for (size_t i = 0; i < kernel_split; ++i) {
    new_pd[i].clear();
  }

  // Clone the kernel's higher-half mappings so the new address space can still
  // execute kernel code / access kernel data after a CR3 switch.
  for (size_t i = kernel_split; i < 1023; ++i) {
    new_pd[i] = directory[i];
  }
  // Slot TEMP_PD_SLOT currently aliases new_pd itself; do not inherit it.
  new_pd[TEMP_PD_SLOT].clear();

  // Self-map the recursive slot so the new address space can walk/modify its
  // own page tables once it becomes active.
  new_pd[1023].set_page_table(
      static_cast<uintptr_t>(new_pd_phys),
      hal::x86::PageFlags::PRESENT | hal::x86::PageFlags::WRITABLE);

  // Tear down the temporary alias.
  directory[TEMP_PD_SLOT].clear();
  HAL_VMM_Flush(VirtualAddress(hal::x86::RECURSIVE_PT_WINDOW + (TEMP_PD_SLOT * 4096)));

  if (!register_root(static_cast<uintptr_t>(new_pd_phys))) {
    HAL_PMM_FreeFrames(new_pd_phys, 1);
    return 0;
  }
  return new_pd_phys;
}

UNDOS_HAL_API_DEF bool HAL_VMM_SetAddressSpaceOwner(PhysicalAddress root, uint32_t pid) noexcept {
  if (!root || !pid) return false;
  const uintptr_t state = g_root_lock.lock_irqsave();
  for (size_t i = 0; i < g_root_count; ++i) {
    if (g_roots[i] == static_cast<uintptr_t>(root) && g_root_owners[i] == 0) {
      g_root_owners[i] = pid;
      g_root_lock.unlock_irqrestore(state);
      return true;
    }
  }
  g_root_lock.unlock_irqrestore(state);
  return false;
}

UNDOS_HAL_API_DEF void HAL_VMM_DestroyAddressSpace(PhysicalAddress root) noexcept {
  if (!root) {
    return;
  }

  auto *directory = hal::x86::get_page_directory();
  directory[TEMP_PD_SLOT].set_page_table(
      static_cast<uintptr_t>(root),
      hal::x86::PageFlags::PRESENT | hal::x86::PageFlags::WRITABLE);
  HAL_VMM_Flush(VirtualAddress(hal::x86::RECURSIVE_PT_WINDOW + (TEMP_PD_SLOT * 4096)));

  const auto *target_pd = reinterpret_cast<hal::x86::page_directory_entry_t *>(
      hal::x86::get_recursive_window(TEMP_PD_SLOT));

  constexpr size_t kernel_split = 768;// 0xC0000000 >> 22
  uintptr_t user_tables[kernel_split]{};
  for (size_t i = 0; i < kernel_split; ++i) {
    if (target_pd[i].is_present()) user_tables[i] = target_pd[i].get_page_table();
  }
  const uint32_t owner_pid = address_space_owner(static_cast<uintptr_t>(root));

  for (const unsigned long user_table: user_tables) {
    if (!user_table) continue;
    directory[TEMP_PD_SLOT].set_page_table(
        user_table, hal::x86::PageFlags::PRESENT | hal::x86::PageFlags::WRITABLE);
    HAL_VMM_Flush(VirtualAddress(hal::x86::RECURSIVE_PT_WINDOW + (TEMP_PD_SLOT * 4096)));
    auto *table = hal::x86::get_recursive_window(TEMP_PD_SLOT);

    if (owner_pid) {
      for (size_t entry = 0; entry < 1024; ++entry) {
        const bool present = table[entry].is_present();
        const bool user = hal::x86::has_flag(
            static_cast<hal::x86::PageFlags>(table[entry].value),
            hal::x86::PageFlags::USER);
        (void) hal::x86::reclaim_owned_mapping(
            owner_pid, present, user, table[entry].get_frame(),
            [](uint32_t pid, uintptr_t frame) {
              return HAL_PMM_FreeOwnedFrames(pid, PhysicalAddress(frame), 1);
            });
      }
    }
    directory[TEMP_PD_SLOT].clear();
    HAL_VMM_Flush(VirtualAddress(hal::x86::RECURSIVE_PT_WINDOW + (TEMP_PD_SLOT * 4096)));
    HAL_PMM_FreeFrames(PhysicalAddress(user_table), 1);
  }

  directory[TEMP_PD_SLOT].clear();
  HAL_VMM_Flush(VirtualAddress(hal::x86::RECURSIVE_PT_WINDOW + (TEMP_PD_SLOT * 4096)));

  unregister_root(static_cast<uintptr_t>(root));
  if (owner_pid) HAL_PMM_UnregisterOwner(owner_pid);
  HAL_PMM_FreeFrames(root, 1);
}

UNDOS_HAL_API_DEF bool HAL_VMM_MapPage(VirtualAddress virtual_addr, PhysicalAddress physical_addr, kernel::vmm::ProtectFlags flags) noexcept {
  const uintptr_t pd_index = static_cast<uintptr_t>(virtual_addr >> 22);
  const uintptr_t pt_index = static_cast<uintptr_t>((virtual_addr >> 12) & 0x3FF);

  PhysicalAddress spare_table{};
  if (!hal::x86::get_page_directory()[pd_index].is_present()) {
    spare_table = HAL_PMM_AllocateFrames(1);
    if (!spare_table) return false;
  }
  const uintptr_t mapping_state = HAL_CPU_SaveAndDisableInterrupts();
  // A preempted allocator can have installed the shared kernel PDE while
  // this call acquired its spare frame. Publish/initialize atomically on UP.
  struct RestoreMapping {
    uintptr_t state;
    PhysicalAddress spare;
    ~RestoreMapping() {
      HAL_CPU_RestoreInterrupts(state);
      if (spare) HAL_PMM_FreeFrames(spare, 1);
    }
  } restore{mapping_state, spare_table};
  if (auto *directory = hal::x86::get_page_directory();
      !directory[pd_index].is_present()) {
    if (!spare_table) return false;
    const PhysicalAddress new_table_phys = spare_table;
    restore.spare = PhysicalAddress(0);

    directory[pd_index].set_page_table(
        static_cast<uintptr_t>(new_table_phys),
        hal::x86::PageFlags::PRESENT       // Present frame
            | hal::x86::PageFlags::WRITABLE// Writable frame
            | hal::x86::PageFlags::USER    // User frame
    );

    // Flush the recursive window address for this page table
    HAL_VMM_Flush(VirtualAddress(hal::x86::RECURSIVE_PT_WINDOW + (pd_index * 4096)));

    auto *page_table = hal::x86::get_page_table(virtual_addr);
    for (size_t i = 0; i < 1024; ++i) {
      page_table[i].clear();
    }

    // We should also flush the directory entry itself, just in case
    HAL_VMM_Flush(virtual_addr);

    if (pd_index >= KERNEL_SPLIT && pd_index < TEMP_PD_SLOT) {
      propagate_kernel_pde(pd_index);
    }
  }

  auto *page_table = hal::x86::get_page_table(virtual_addr);
  if (page_table[pt_index].is_present()) {
    return false;
  }

  // Translate generic flags to raw x86 bits
  page_table[pt_index].set_frame(static_cast<uintptr_t>(physical_addr), hal::x86::translate_flags(flags));
  if (virtual_addr.value < 0xC0000000 &&
      kernel::vmm::has_flag(flags, kernel::vmm::ProtectFlags::USER)) {
    const uint32_t owner = ::address_space_owner(
        static_cast<uintptr_t>(HAL_VMM_GetCurrentTranslationRoot()));
    if (owner) hal::x86::assign_frame_owner(physical_addr, owner);
  }

  return true;
}

UNDOS_HAL_API_DEF void HAL_VMM_UnmapPage(VirtualAddress virtual_addr) noexcept {
  const uintptr_t mapping_state = HAL_CPU_SaveAndDisableInterrupts();
  struct RestoreMapping {
    uintptr_t state;
    ~RestoreMapping() { HAL_CPU_RestoreInterrupts(state); }
  } restore{mapping_state};
  const uintptr_t pd_index = static_cast<uintptr_t>(virtual_addr >> 22);
  const uintptr_t pt_index = static_cast<uintptr_t>((virtual_addr >> 12) & 0x3FF);

  if (auto *directory = hal::x86::get_page_directory();
      directory[pd_index].is_present()) {
    auto *page_table = hal::x86::get_page_table(virtual_addr);

    // 1. Clear the target page frame entry
    page_table[pt_index].clear();

    // Shared kernel tables are retained regardless of occupancy. Scanning
    // them on every temporary syscall unmap only extends the IRQ-off span.
    if (pd_index >= KERNEL_SPLIT) return;

    // 2. Check if the parent Page Table is now entirely empty
    bool table_is_empty = true;
    for (size_t i = 0; i < 1024; ++i) {
      if (page_table[i].is_present()) {
        table_is_empty = false;
        break;// Found an active mapping; we cannot free this table yet
      }
    }

    // 3. If no pages remain in this table, reclaim the page table frame itself!
    //    Kernel-half tables are shared by every address space and stay.
    if (table_is_empty && pd_index < KERNEL_SPLIT) {
      // Grab the physical address of the page table before we sever the connection
      const PhysicalAddress page_table_phys = directory[pd_index].get_page_table();

      // Clear the directory entry so the MMU stops looking here
      directory[pd_index].clear();

      // Return the page table's frame back to the global physical pool
      HAL_PMM_FreeFrames(page_table_phys, 1);

      // Context note: Because the page table itself is now gone from virtual space,
      // we must also ensure the recursive mapping area for this table is invalidated
      // by flushing the TLB for the window area, or letting the global flush handle it.
    }
  }
}

// Force the hardware to flush its address translation caches (TLB)
UNDOS_HAL_API_DEF void HAL_VMM_Flush(VirtualAddress virtual_addr) noexcept {
  asm volatile("invlpg (%0)" ::"r"(static_cast<uintptr_t>(virtual_addr)) : "memory");
}

namespace hal::x86 {
void ensure_page_user_writable(kernel::VirtualAddress virtual_addr, kernel::PhysicalAddress physical_addr) noexcept {
  const uintptr_t pd_index = virtual_addr.value >> 22;
  const uintptr_t pt_index = (virtual_addr.value >> 12) & 0x3FF;

  auto *directory = get_page_directory();
  constexpr auto user_writable = PageFlags::PRESENT | PageFlags::WRITABLE | PageFlags::USER;

  if (!directory[pd_index].is_present()) {
    const PhysicalAddress new_table_phys = HAL_PMM_AllocateFrames(1);
    if (!new_table_phys) {
      return;
    }

    directory[pd_index].set_page_table(static_cast<uintptr_t>(new_table_phys), user_writable);
    HAL_VMM_Flush(VirtualAddress(RECURSIVE_PT_WINDOW + (pd_index * 4096)));

    auto *page_table = get_page_table(virtual_addr);
    for (size_t i = 0; i < 1024; ++i) {
      page_table[i].clear();
    }
    HAL_VMM_Flush(virtual_addr);
  } else if (!has_flag(static_cast<PageFlags>(directory[pd_index].value), PageFlags::USER)) {
    // Upgrade a pre-existing supervisor-only page table (e.g. a transitional
    // identity map left behind by the Stage 1.5 boot loader) so pages within
    // it *can* be marked user-accessible; individual page table entries below
    // still control their own USER bit.
    directory[pd_index].set_page_table(directory[pd_index].get_page_table(), user_writable);
    HAL_VMM_Flush(VirtualAddress(RECURSIVE_PT_WINDOW + (pd_index * 4096)));
  }

  if (auto *page_table = get_page_table(virtual_addr); !page_table[pt_index].is_present() ||
                                                       !has_flag(static_cast<PageFlags>(page_table[pt_index].value), PageFlags::USER) ||
                                                       !has_flag(static_cast<PageFlags>(page_table[pt_index].value), PageFlags::WRITABLE)) {
    page_table[pt_index].set_frame(static_cast<uintptr_t>(physical_addr), user_writable);
    HAL_VMM_Flush(virtual_addr);
  }
}
}// namespace hal::x86
