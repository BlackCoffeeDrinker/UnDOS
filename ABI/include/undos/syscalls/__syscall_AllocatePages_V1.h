#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Allocates zeroed pages, rounding size up to 4096. Read is required; unknown
 * flags and no-access/write-only mappings reject. i386 does not enforce NX.
 */
struct __SYS_MEM_ALLOCATEPAGES_V1 {
    /** Number of bytes to allocate (rounded up to a page boundary). */
    uint32_t size;
    /** Protection to apply to the newly allocated pages. */
    enum MemoryProtectFlags flags;
    /** Base address of the allocated region. */
    uint32_t address;
};

constexpr size_t   __SYS_MEM_ALLOCATEPAGES_V1_SIZE = sizeof(struct __SYS_MEM_ALLOCATEPAGES_V1);
constexpr uint32_t __SYS_MEM_ALLOCATEPAGES_V1_CALLNUMBER = 210;

