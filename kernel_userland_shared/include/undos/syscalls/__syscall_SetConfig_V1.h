#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Sets TTY configuration flags and window dimensions. */
struct __SYS_TTY_SETCONFIG_V1 {
    uint32_t handle;
    enum TtyModeFlags flags;
    uint16_t rows;
    uint16_t cols;
};

constexpr size_t   __SYS_TTY_SETCONFIG_V1_SIZE = sizeof(struct __SYS_TTY_SETCONFIG_V1);
constexpr uint32_t __SYS_TTY_SETCONFIG_V1_CALLNUMBER = 251;

