#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/** Reads data from an open object handle into `data`. */
struct __SYS_IO_READ_V1 {
    /** Open handle to read from. */
    HANDLE handle;
    /** Buffer that receives the data read. */
    uint8_t* data;
    uint32_t dataLength;
};

constexpr size_t   __SYS_IO_READ_V1_SIZE = sizeof(struct __SYS_IO_READ_V1);
constexpr uint32_t __SYS_IO_READ_V1_CALLNUMBER = 222;

