#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Creates a directory at a nonexisting absolute path of at most 255 bytes.
 * Requires FAT short names and an existing parent directory.
 * Open directory handles on the volume block creation.
 * Device failures can leave partial changes; no crash consistency is promised.
 */
struct __SYS_IO_CREATEDIRECTORY_V1 {
    const char* path;
    size_t pathLength;
};

constexpr size_t   __SYS_IO_CREATEDIRECTORY_V1_SIZE = sizeof(struct __SYS_IO_CREATEDIRECTORY_V1);
constexpr uint32_t __SYS_IO_CREATEDIRECTORY_V1_CALLNUMBER = 233;

