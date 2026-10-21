#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

struct __SYS_IO_OPEN_V1 {
    /** Path to the object to open. */
    const char* path;
    size_t pathLength;
    /** Open mode (read, write, append). */
    enum FileOpenMode mode;
    /** Handle to receive the opened object. */
    HANDLE handle;
};

constexpr size_t   __SYS_IO_OPEN_V1_SIZE = sizeof(struct __SYS_IO_OPEN_V1);
constexpr uint32_t __SYS_IO_OPEN_V1_CALLNUMBER = 220;

