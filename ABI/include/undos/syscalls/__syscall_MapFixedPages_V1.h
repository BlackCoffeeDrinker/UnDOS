#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Maps zeroed anonymous user pages at a page-aligned address selected by the
 * caller. Read is required; unknown flags and no-access/write-only mappings
 * reject. i386 does not enforce NX.
 */
struct __SYS_MEM_MAPFIXEDPAGES_V1 {
    uint32_t address;
    uint32_t size;
    enum MemoryProtectFlags flags;
};

constexpr size_t   __SYS_MEM_MAPFIXEDPAGES_V1_SIZE = sizeof(struct __SYS_MEM_MAPFIXEDPAGES_V1);
constexpr uint32_t __SYS_MEM_MAPFIXEDPAGES_V1_CALLNUMBER = 213;

