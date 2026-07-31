#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_FS_CLOSE_V1 {
    /** Open file descriptor to close. */
    uint32_t fileDescriptor;
};

constexpr size_t   __SYS_FS_CLOSE_V1_SIZE = sizeof(struct __SYS_FS_CLOSE_V1);
constexpr uint32_t __SYS_FS_CLOSE_V1_CALLNUMBER = 221;

