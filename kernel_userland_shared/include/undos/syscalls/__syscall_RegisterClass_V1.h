#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Registers a window class; wndProc is stored opaquely and called by userland. */
struct __SYS_USER_REGISTERCLASS_V1 {
    const char* name;
    size_t nameLength;
    uint32_t wndProc;
    uint32_t background;
};

constexpr size_t   __SYS_USER_REGISTERCLASS_V1_SIZE = sizeof(struct __SYS_USER_REGISTERCLASS_V1);
constexpr uint32_t __SYS_USER_REGISTERCLASS_V1_CALLNUMBER = 400;

