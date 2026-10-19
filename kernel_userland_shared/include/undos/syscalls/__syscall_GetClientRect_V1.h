#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Client bounds {0,0,width,height}; empty while minimized. */
struct __SYS_USER_GETCLIENTRECT_V1 {
    uint32_t hwnd;
    struct Rect rect;
};

constexpr size_t   __SYS_USER_GETCLIENTRECT_V1_SIZE = sizeof(struct __SYS_USER_GETCLIENTRECT_V1);
constexpr uint32_t __SYS_USER_GETCLIENTRECT_V1_CALLNUMBER = 416;

