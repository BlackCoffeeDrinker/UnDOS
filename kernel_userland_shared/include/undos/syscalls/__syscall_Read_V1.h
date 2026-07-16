#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Reads data from an open file descriptor into `data`. */
struct __SYS_FS_READ_V1 {
    /** Open file descriptor to read from. */
    uint32_t fileDescriptor;
    /** Buffer that receives the data read from the file. */
    uint8_t* data;
    uint32_t dataLength;
};

constexpr size_t   __SYS_FS_READ_V1_SIZE = sizeof(struct __SYS_FS_READ_V1);
constexpr uint32_t __SYS_FS_READ_V1_CALLNUMBER = 222;

