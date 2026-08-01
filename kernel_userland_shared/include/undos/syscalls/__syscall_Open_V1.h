#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_FS_OPEN_V1 {
    /** Path to the file to open. */
    const char* path;
    size_t pathLength;
    /** Open mode (read, write, append). */
    enum FileOpenMode mode;
    /** File descriptor to receive the open file descriptor. */
    uint32_t fileDescriptor;
};

constexpr size_t   __SYS_FS_OPEN_V1_SIZE = sizeof(struct __SYS_FS_OPEN_V1);
constexpr uint32_t __SYS_FS_OPEN_V1_CALLNUMBER = 220;

