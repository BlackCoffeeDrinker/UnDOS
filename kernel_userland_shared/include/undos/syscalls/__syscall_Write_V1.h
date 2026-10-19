#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/** Writes data from `data` to an open object handle. */
struct __SYS_IO_WRITE_V1 {
    /** Open handle to write to. */
    HANDLE handle;
    /** Buffer containing the data to write. */
    const uint8_t* data;
    uint32_t dataLength;
};

constexpr size_t   __SYS_IO_WRITE_V1_SIZE = sizeof(struct __SYS_IO_WRITE_V1);
constexpr uint32_t __SYS_IO_WRITE_V1_CALLNUMBER = 223;

