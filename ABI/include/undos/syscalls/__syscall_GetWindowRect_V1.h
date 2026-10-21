#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Window rectangle in screen coordinates. */
struct __SYS_USER_GETWINDOWRECT_V1 {
    uint32_t hwnd;
    struct Rect rect;
};

constexpr size_t   __SYS_USER_GETWINDOWRECT_V1_SIZE = sizeof(struct __SYS_USER_GETWINDOWRECT_V1);
constexpr uint32_t __SYS_USER_GETWINDOWRECT_V1_CALLNUMBER = 405;

