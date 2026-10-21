#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_USER_MOVEWINDOW_V1 {
    uint32_t hwnd;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

constexpr size_t   __SYS_USER_MOVEWINDOW_V1_SIZE = sizeof(struct __SYS_USER_MOVEWINDOW_V1);
constexpr uint32_t __SYS_USER_MOVEWINDOW_V1_CALLNUMBER = 404;

