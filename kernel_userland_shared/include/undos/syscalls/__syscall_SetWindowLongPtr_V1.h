#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_USER_SETWINDOWLONGPTR_V1 {
    uint32_t hwnd;
    uint32_t index;
    uint32_t value;
    uint32_t previous;
};

constexpr size_t   __SYS_USER_SETWINDOWLONGPTR_V1_SIZE = sizeof(struct __SYS_USER_SETWINDOWLONGPTR_V1);
constexpr uint32_t __SYS_USER_SETWINDOWLONGPTR_V1_CALLNUMBER = 413;

