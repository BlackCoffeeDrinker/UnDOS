#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/**
 * Enumerates one directory entry by zero-based index, excluding . and ...
 * Requires a readable directory handle. found=0 means end of directory.
 * name uses query-length-then-fill, including its terminating NUL.
 * Indices are not snapshots: restart enumeration after a mutation.
 * On insufficient capacity returns INVALID_SIZE without truncation.
 */
struct __SYS_IO_READDIRECTORY_V1 {
    HANDLE handle;
    uint32_t index;
    char* name;
    size_t nameLength;
    struct FileInformation information;
    uint32_t found;
};

constexpr size_t   __SYS_IO_READDIRECTORY_V1_SIZE = sizeof(struct __SYS_IO_READDIRECTORY_V1);
constexpr uint32_t __SYS_IO_READDIRECTORY_V1_CALLNUMBER = 227;

