#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_IO_CLOSE_V1 {
    /** Open handle to close. */
    uint32_t handle;
};

constexpr size_t   __SYS_IO_CLOSE_V1_SIZE = sizeof(struct __SYS_IO_CLOSE_V1);
constexpr uint32_t __SYS_IO_CLOSE_V1_CALLNUMBER = 221;

