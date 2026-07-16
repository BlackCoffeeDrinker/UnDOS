#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Writes data from `data` to an open file descriptor. */
struct __SYS_FS_WRITE_V1 {
    /** Open file descriptor to write to. */
    uint32_t fileDescriptor;
    /** Buffer containing the data to write to the file. */
    const uint8_t* data;
    uint32_t dataLength;
};

constexpr size_t   __SYS_FS_WRITE_V1_SIZE = sizeof(struct __SYS_FS_WRITE_V1);
constexpr uint32_t __SYS_FS_WRITE_V1_CALLNUMBER = 223;

