#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Set a same-process window title (at most 63 bytes plus terminator). */
struct __SYS_USER_SETWINDOWTEXT_V1 {
    uint32_t hwnd;
    const char* text;
    size_t textLength;
};

constexpr size_t   __SYS_USER_SETWINDOWTEXT_V1_SIZE = sizeof(struct __SYS_USER_SETWINDOWTEXT_V1);
constexpr uint32_t __SYS_USER_SETWINDOWTEXT_V1_CALLNUMBER = 418;

