#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Immediately destroys the window and descendants and removes their queued
 * messages. Userland dispatches any Destroy callback before this syscall;
 * no message may subsequently target an invalidated handle.
 */
struct __SYS_USER_DESTROYWINDOW_V1 {
    uint32_t hwnd;
};

constexpr size_t   __SYS_USER_DESTROYWINDOW_V1_SIZE = sizeof(struct __SYS_USER_DESTROYWINDOW_V1);
constexpr uint32_t __SYS_USER_DESTROYWINDOW_V1_CALLNUMBER = 402;

