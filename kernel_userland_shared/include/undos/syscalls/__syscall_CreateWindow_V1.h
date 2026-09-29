#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Creates a top-level window owned by the calling thread. */
struct __SYS_USER_CREATEWINDOW_V1 {
    const char* className;
    size_t classNameLength;
    const char* title;
    size_t titleLength;
    enum WindowStyle style;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t parent;
    uint32_t userData;
    uint32_t hwnd;
};

constexpr size_t   __SYS_USER_CREATEWINDOW_V1_SIZE = sizeof(struct __SYS_USER_CREATEWINDOW_V1);
constexpr uint32_t __SYS_USER_CREATEWINDOW_V1_CALLNUMBER = 401;

