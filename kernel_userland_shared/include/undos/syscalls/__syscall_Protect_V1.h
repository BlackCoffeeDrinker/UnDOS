#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Changes the protection of an existing page range (e.g. mark executable). */
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

