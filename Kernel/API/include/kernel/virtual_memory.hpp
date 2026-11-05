#pragma once

#include <kernel/__core.hpp>
#include <kernel/kerror.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/memory/page_fault.hpp>
#include <kernel/memory/protect.hpp>
#include <kernel/memory/vad.hpp>
#include <kernel/rw_mutex.hpp>
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
  uint32_t owner_pid = 0;                                     // Nonzero for process-owned user address spaces
  VirtualAddress current_base = VirtualAddress::Invalid;
  VirtualAddress limit = VirtualAddress::Invalid;
  // Guards `vads` and the page tables of a user address space: exclusive to
  // change them, shared to rely on them (user copies, page faults). Order:
  // lock, then the arena mutex, then spinlocks. Never fault while holding it
  // exclusively. The kernel address space uses the arena mutex instead.
  RwMutex lock;
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
 * @note This implementation supports only the currently active address space;
 *       other address spaces return Unsupported without mapping.
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
 * The caller owns exactly one synchronous release with the exact returned
 * pointer, requested byte length, and original AddressSpace identity. The
 * source space and frames must stay alive through release; frames are not
 * pinned here. Do not retain or release the pointer after successful unmap.
 * Stale raw releases after same-address reuse are unsupported caller misuse.
 * Do not call from hardware IRQ context; contended arena work requires a
 * schedulable current thread.
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
 * and returns the reserved extent to borrowed-alias reuse only after every
 * translation is absent. Failed cleanup is diagnosed and quarantined.
 * Validates the exact live alias before changing PTEs and claims its release
 * exclusively. The pointer becomes invalid after success. No generation-token
 * protection is provided against a duplicate after successful release/reuse.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_VMM_UnmapBorrowed,
    void *addr, size_t size, kernel::vmm::AddressSpace &as);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_CopyFromUser
 *
 * Copies `size` bytes from `userSrc` (as seen from `as`) into kernel memory at
 * `dst`. When `as` is the active address space the copy is direct: every page
 * is checked against its VAD (USER + READ) and must be present, so the copy
 * never faults. Otherwise it falls back to a borrowed alias. Not for IRQ
 * context.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_VMM_CopyFromUser,
    kernel::vmm::AddressSpace &as, void *dst, const void *userSrc, size_t size);

/**
 * @ingroup VMM
 * @brief Method KE_VMM_CopyToUser
 *
 * Copies `size` bytes from kernel memory at `src` to `userDst` in `as`. The
 * direct path requires USER + WRITE in the VAD and in the page entry; the
 * kernel checks this itself because ring 0 may ignore write protection
 * (i386). Each page goes through one preparation hook first, the single place
 * where copy-on-write / demand paging will make it private and writable.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_VMM_CopyToUser,
    kernel::vmm::AddressSpace &as, void *userDst, const void *src, size_t size);

UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_VMM_HandlePagefault,
    const kernel::PageFaultInfo &info);
