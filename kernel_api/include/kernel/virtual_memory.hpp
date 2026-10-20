#pragma once

#include <kernel/__core.hpp>
#include <kernel/kerror.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/memory/page_fault.hpp>
#include <kernel/memory/protect.hpp>
#include <kernel/memory/vad.hpp>
#include <utility.hpp>
#include <expected.hpp>

namespace kernel {
struct KMemoryBackingObject;
}// namespace kernel

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
  PhysicalAddress translation_root = PhysicalAddress::Invalid;// Architecture-specific root (e.g., CR3, TTBR, satp)
  uint32_t asid = 0;                                          // Address Space Identifier (if supported by arch)
  VirtualAddress current_base = VirtualAddress::Invalid;
  VirtualAddress limit = VirtualAddress::Invalid;
};
}// namespace kernel::vmm

namespace kernel {
using VmmRegionResult = kstd::expected<void *, kstd::error_code>;
using VmmBackingObjectMapResult = kstd::expected<vmm::VirtualAddressDescriptor *, kstd::error_code>;
using VmmDirectBackingResult = kstd::expected<KObjectPtr<KMemoryBackingObject>, kstd::error_code>;
}// namespace kernel

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
    kernel::VmmRegionResult,
    KE_VMM_AllocateRegion,
    kernel::vmm::AddressSpace &as, size_t size, kernel::vmm::ProtectFlags flags);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_FreeRegion
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_VMM_FreeRegion,
    kernel::vmm::AddressSpace &as, void *addr);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_MapBackingObject
 *
 * Reserves a range of address space in `as` and creates a VAD mapping the
 * given backing object into it. Returns the created VAD or the reason the
 * mapping could not be reserved.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::VmmBackingObjectMapResult,
    KE_VMM_MapBackingObject,
    kernel::vmm::AddressSpace &as, const kernel::KObjectPtr<kernel::KMemoryBackingObject> &backing,
    size_t size, kernel::vmm::ProtectFlags flags);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_AllocateUserData
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::VmmRegionResult,
    KE_VMM_AllocateUserData,
    kernel::vmm::AddressSpace &as, size_t size);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_AllocateUserProcess
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::VmmRegionResult,
    KE_VMM_AllocateUserProcess,
    kernel::vmm::AddressSpace &as, size_t size);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_MapPhysical
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
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

/**
 * @ingroup VMM
 * @brief Method KE_VMM_CreateDirectBacking
 *
 * Creates a KMemoryBackingObject for memory that is already mapped and
 * resident at a fixed virtual address (e.g. a hardware MMIO window mapped
 * via KE_VMM_MapPhysical), rather than demand-paged. Used by drivers (e.g.
 * gfx_vga) that need to expose such memory through a KSurface.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::VmmDirectBackingResult,
    KE_VMM_CreateDirectBacking,
    kernel::VirtualAddress directAddress, size_t size);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_MapBorrowed
 *
 * Maps `size` bytes starting at `addr` (as seen from `as`) into the kernel's
 * address space, so kernel code can safely read/write memory that lives in
 * another (typically user-mode) address space. Returns the kernel-mapped
 * pointer or the reason mapping failed. Callers must release the mapping with
 * KE_VMM_UnmapBorrowed once done.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::VmmRegionResult,
    KE_VMM_MapBorrowed,
    void *addr, size_t size, kernel::vmm::AddressSpace &as);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_UnmapBorrowed
 *
 * Reverses KE_VMM_MapBorrowed: unmaps the kernel-side pages backing `addr`
 * and frees the kernel virtual region that was reserved for the borrow.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_VMM_UnmapBorrowed,
    void *addr, size_t size, kernel::vmm::AddressSpace &as);

UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_VMM_HandlePagefault,
    const kernel::PageFaultInfo &info);
