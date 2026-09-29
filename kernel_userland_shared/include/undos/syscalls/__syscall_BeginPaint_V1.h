#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Returns a DC clipped to the visible part of the invalid area, origin at the window. */
struct __SYS_USER_BEGINPAINT_V1 {
    uint32_t hwnd;
    uint32_t dc;
    struct PaintInfo paint;
};

constexpr size_t   __SYS_USER_BEGINPAINT_V1_SIZE = sizeof(struct __SYS_USER_BEGINPAINT_V1);
constexpr uint32_t __SYS_USER_BEGINPAINT_V1_CALLNUMBER = 407;

