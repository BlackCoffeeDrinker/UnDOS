#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Deletes a file or empty directory at an absolute path of at most 255 bytes.
 * Requires FAT short names. Roots, . and .. are rejected.
 * Open source entries, open directories and read-only entries block deletion.
 * Recursive deletion is unsupported. Device failures can leave partial changes;
 * no crash consistency is promised.
 */
struct __SYS_IO_DELETEFILE_V1 {
    const char* path;
    size_t pathLength;
};

constexpr size_t   __SYS_IO_DELETEFILE_V1_SIZE = sizeof(struct __SYS_IO_DELETEFILE_V1);
constexpr uint32_t __SYS_IO_DELETEFILE_V1_CALLNUMBER = 232;

