#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Moves a file, or renames a directory within the same parent.
 * Both paths must be absolute, at most 255 bytes, with FAT short names.
 * The destination must not exist and must be on the same mount.
 * Open source entries and open directories block the operation.
 * Cross-parent directory moves and cross-volume moves are unsupported.
 * Device failures can leave partial changes; no crash consistency is promised.
 */
struct __SYS_IO_MOVEFILE_V1 {
    const char* source;
    size_t sourceLength;
    const char* destination;
    size_t destinationLength;
};

constexpr size_t   __SYS_IO_MOVEFILE_V1_SIZE = sizeof(struct __SYS_IO_MOVEFILE_V1);
constexpr uint32_t __SYS_IO_MOVEFILE_V1_CALLNUMBER = 231;

