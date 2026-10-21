#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Copies one file to a nonexisting destination on the same mount.
 * Both paths must be absolute, at most 255 bytes, with FAT short names.
 * Open source entries and open directories block the operation.
 * Directory copies and cross-volume copies are unsupported.
 * Device failures can leave partial changes; no crash consistency is promised.
 */
struct __SYS_IO_COPYFILE_V1 {
    const char* source;
    size_t sourceLength;
    const char* destination;
    size_t destinationLength;
};

constexpr size_t   __SYS_IO_COPYFILE_V1_SIZE = sizeof(struct __SYS_IO_COPYFILE_V1);
constexpr uint32_t __SYS_IO_COPYFILE_V1_CALLNUMBER = 230;

