#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * File or directory information. Attributes use the FAT attribute bits:
 * read-only=1, hidden=2, system=4, directory=16, archive=32.
 * Timestamps and long filenames are not supplied by this interface.
 */
struct FileInformation {
    uint64_t size;
    uint32_t attributes;
};
