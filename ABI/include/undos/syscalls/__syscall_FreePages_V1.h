#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Frees a whole eager anonymous region; base must be page aligned and size,
 * rounded up to 4096, must equal the region size. No partial frees or aliases.
 */
struct __SYS_MEM_FREEPAGES_V1 {
    /** Base address of the region to free. */
    uint32_t address;
    /** Size in bytes of the region to free. */
    uint32_t size;
};

constexpr size_t   __SYS_MEM_FREEPAGES_V1_SIZE = sizeof(struct __SYS_MEM_FREEPAGES_V1);
constexpr uint32_t __SYS_MEM_FREEPAGES_V1_CALLNUMBER = 211;

