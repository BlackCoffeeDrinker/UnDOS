#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** One of ShowCommand. */
struct __SYS_USER_SHOWWINDOW_V1 {
    uint32_t hwnd;
    uint32_t command;
};

constexpr size_t   __SYS_USER_SHOWWINDOW_V1_SIZE = sizeof(struct __SYS_USER_SHOWWINDOW_V1);
constexpr uint32_t __SYS_USER_SHOWWINDOW_V1_CALLNUMBER = 403;

