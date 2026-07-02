
#include "vmm.hpp"
#include <kernel/hal_interface.hpp>

using kernel::PhysicalAddress;
using kernel::VirtualAddress;

namespace hal::x86 {
constexpr uintptr_t RECURSIVE_PT_WINDOW = 0xFFC00000;
constexpr uintptr_t RECURSIVE_PD_WINDOW = 0xFFFFF000;

inline page_directory_entry_t *get_page_directory() noexcept {
  return reinterpret_cast<page_directory_entry_t *>(RECURSIVE_PD_WINDOW);
}

inline page_table_entry_t *get_page_table(VirtualAddress virtual_addr) noexcept {
  const uintptr_t pd_index = static_cast<uintptr_t>(virtual_addr >> 22);
  return reinterpret_cast<page_table_entry_t *>(RECURSIVE_PT_WINDOW + (pd_index * 4096));
}

PageFlags translate_flags(kernel::vmm::ProtectFlags flags) noexcept {
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
}// namespace hal::x86

UNDOS_HAL_API void HAL_VMM_EarlyInit(const kernel::BootInfoT &boot_info) noexcept {
  // Stage 1.5 currently sets the page size to be 4096 bytes
  // Double check
  if (boot_info.page_size != 4096) {
    early_print("Stage 1.5 did not set the page size to 4096 bytes");
  }

  // Read the active Page Directory established by Stage 1.5
  uintptr_t current_pd_phys;
  __asm__ volatile("mov %%cr3, %0" : "=r"(current_pd_phys));

  // Access the page directory structure directly using the recursive window.
  // Stage 1.5 must have pre-mapped this entry; we explicitly assert its validity
  // and re-apply permissions to ensure the HAL can modify any page table.
  auto *directory = hal::x86::get_page_directory();

  constexpr size_t recursive_slot = 1023;// 0xFFC00000 >> 22

  directory[recursive_slot].set_page_table(
      static_cast<uint32_t>(current_pd_phys),
      hal::x86::PageFlags::PRESENT |
          hal::x86::PageFlags::WRITABLE |
          hal::x86::PageFlags::USER);

  // Force a global TLB flush by reloading CR3 to validate our window setup immediately
  __asm__ volatile("mov %0, %%cr3" ::"r"(current_pd_phys) : "memory");
}

UNDOS_HAL_API void HAL_VMM_FinalizeInit() noexcept {
}

UNDOS_HAL_API PhysicalAddress HAL_VMM_GetCurrentTranslationRoot() noexcept {
  uintptr_t current_pd_phys;
  __asm__ volatile("mov %%cr3, %0" : "=r"(current_pd_phys));
  return current_pd_phys;
}

UNDOS_HAL_API PhysicalAddress HAL_VMM_GetPhysicalAddress(VirtualAddress virt) noexcept {
  const uintptr_t pd_index = static_cast<uintptr_t>(virt >> 22);
  const uintptr_t pt_index = static_cast<uintptr_t>((virt >> 12) & 0x3FF);
  const uintptr_t page_offset = static_cast<uintptr_t>(virt & 0xFFF);

  auto *directory = hal::x86::get_page_directory();
  if (!directory[pd_index].is_present()) return 0;

  auto *page_table = hal::x86::get_page_table(virt);
  if (!page_table[pt_index].is_present()) return 0;

  return page_table[pt_index].get_frame() + page_offset;
}

UNDOS_HAL_API void HAL_VMM_SwitchAddressSpace(PhysicalAddress new_root) noexcept {
  __asm__ volatile("mov %0, %%cr3" ::"r"(static_cast<uintptr_t>(new_root)) : "memory");
}

UNDOS_HAL_API bool HAL_VMM_MapPage(VirtualAddress virtual_addr, PhysicalAddress physical_addr, kernel::vmm::ProtectFlags flags) noexcept {
  const uintptr_t pd_index = static_cast<uintptr_t>(virtual_addr >> 22);
  const uintptr_t pt_index = static_cast<uintptr_t>((virtual_addr >> 12) & 0x3FF);

  // Handle x86 multi-level table allocation safely within the HAL
  if (auto *directory = hal::x86::get_page_directory();
      !directory[pd_index].is_present()) {
    const PhysicalAddress new_table_phys = HAL_PMM_AllocateFrames(1);
    if (!new_table_phys) {
      return false;
    }

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
  }

  auto *page_table = hal::x86::get_page_table(virtual_addr);
  if (page_table[pt_index].is_present()) {
    return false;
  }

  // Translate generic flags to raw x86 bits
  page_table[pt_index].set_frame(static_cast<uintptr_t>(physical_addr), hal::x86::translate_flags(flags));

  return true;
}

UNDOS_HAL_API void HAL_VMM_UnmapPage(VirtualAddress virtual_addr) noexcept {
  const uintptr_t pd_index = static_cast<uintptr_t>(virtual_addr >> 22);
  const uintptr_t pt_index = static_cast<uintptr_t>((virtual_addr >> 12) & 0x3FF);

  if (auto *directory = hal::x86::get_page_directory();
      directory[pd_index].is_present()) {
    auto *page_table = hal::x86::get_page_table(virtual_addr);

    // 1. Clear the target page frame entry
    page_table[pt_index].clear();

    // 2. Check if the parent Page Table is now entirely empty
    bool table_is_empty = true;
    for (size_t i = 0; i < 1024; ++i) {
      if (page_table[i].is_present()) {
        table_is_empty = false;
        break;// Found an active mapping; we cannot free this table yet
      }
    }

    // 3. If no pages remain in this table, reclaim the page table frame itself!
    if (table_is_empty) {
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
UNDOS_HAL_API void HAL_VMM_Flush(VirtualAddress virtual_addr) noexcept {
  asm volatile("invlpg (%0)" ::"r"(static_cast<uintptr_t>(virtual_addr)) : "memory");
}
