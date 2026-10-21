#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Reserved: currently rejects with INVALID_ARGUMENT (no partial VAD updates). */
struct __SYS_MEM_PROTECT_V1 {
    /** Base address of the region to reprotect. */
    uint32_t address;
    /** Size in bytes of the region to reprotect. */
    uint32_t size;
    /** New protection to apply. */
    enum MemoryProtectFlags flags;
};

constexpr size_t   __SYS_MEM_PROTECT_V1_SIZE = sizeof(struct __SYS_MEM_PROTECT_V1);
constexpr uint32_t __SYS_MEM_PROTECT_V1_CALLNUMBER = 212;

