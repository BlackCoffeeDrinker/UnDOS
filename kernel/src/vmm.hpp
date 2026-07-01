
#pragma once

#include "Core.hpp"
#include <kernel/memory/vad.hpp>

#include "common/DoubleListLink.hpp"

namespace kernel::vmm {
// Initializes the raw hardware paging structures (e.g., sets up recursive mapping)
void init(const BootInfoT &boot_info) noexcept;
void late_init() noexcept;

/**
 * @brief Platform-agnostic representation of a virtual address space.
 * 
 * This structure is designed to be cross-platform (i386, ARM, MIPS, RISC-V, PPC):
 * - i386: translation_root = CR3 physical address, asid = unused (or PCID).
 * - ARM: translation_root = TTBR0 physical address, asid = ASID from CONTEXTIDR or TTBR.
 * - MIPS: translation_root = Page Table base, asid = ASID.
 * - RISC-V: translation_root = PPN from satp, asid = ASID from satp.
 * - PPC: translation_root = SDR1 or Segment Table base, asid = LPID/ASID.
 */
struct AddressSpace {
  VadTree vads;
  PhysicalAddress translation_root;// Architecture-specific root (e.g., CR3, TTBR, satp)
  uint32_t asid;                   // Address Space Identifier (if supported by arch)
  VirtualAddress current_base;
  VirtualAddress limit;

  // Prepare for VAD tracking
  void *allocate_region(size_t size, ProtectFlags flags) noexcept;
  void free_region(void *addr) noexcept;

  // User-space allocation helpers
  void *allocate_user_data(size_t size) noexcept;
  void *allocate_user_process(size_t size) noexcept;
};

// The kernel's own address space
AddressSpace *get_kernel_address_space() noexcept;

void *allocate_user_memory(AddressSpace &as, size_t size, ProtectFlags flags) noexcept;
}// namespace kernel::vmm
