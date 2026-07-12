#pragma once

#include <kernel/__core.hpp>
#include <kernel/memory/protect.hpp>
#include <kernel/memory/vad.hpp>
#include <utility.hpp>

namespace kernel::vmm {
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
};
}// namespace kernel::vmm

/**
 * @ingroup VMM
 * @brief Method KE_Malloc
 *
 */
UNDOS_KERNEL_PUBLIC_V1API([[nodiscard]] void *, KE_Malloc, size_t size);

/**
 * @ingroup VMM
 * @brief Method KE_Free
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_Free, void *ptr);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_AllocateRegion
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void *,
    KE_VMM_AllocateRegion,
    kernel::vmm::AddressSpace &as, size_t size, kernel::vmm::ProtectFlags flags);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_FreeRegion
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_VMM_FreeRegion,
    kernel::vmm::AddressSpace &as, void *addr);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_AllocateUserData
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void *,
    KE_VMM_AllocateUserData,
    kernel::vmm::AddressSpace &as, size_t size);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_AllocateUserProcess
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void *,
    KE_VMM_AllocateUserProcess,
    kernel::vmm::AddressSpace &as, size_t size);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_MapPhysical
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_VMM_MapPhysical,
    kernel::vmm::AddressSpace &as, kernel::VirtualAddress virt, kernel::PhysicalAddress phys, kernel::vmm::ProtectFlags flags);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_GetKernelAddressSpace
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::vmm::AddressSpace &,
    KE_VMM_GetKernelAddressSpace);
