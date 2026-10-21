#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Memory protection bit flags, mirrors kernel::vmm::ProtectFlags. */
enum MemoryProtectFlags {
    /** No access. */
    MemoryProtectFlags_None = 0,
    /** Page is readable. */
    MemoryProtectFlags_Read = 1,
    /** Page is writable. */
    MemoryProtectFlags_Write = 2,
    /** Page is executable. */
    MemoryProtectFlags_Execute = 4,
};
