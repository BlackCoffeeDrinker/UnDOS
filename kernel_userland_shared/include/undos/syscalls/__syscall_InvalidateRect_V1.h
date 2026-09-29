#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Adds rect (window coordinates) to the invalid area; whole != 0 invalidates everything. */
struct __SYS_USER_INVALIDATERECT_V1 {
    uint32_t hwnd;
    struct Rect rect;
    uint32_t whole;
};

constexpr size_t   __SYS_USER_INVALIDATERECT_V1_SIZE = sizeof(struct __SYS_USER_INVALIDATERECT_V1);
constexpr uint32_t __SYS_USER_INVALIDATERECT_V1_CALLNUMBER = 406;

