#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Queues a message; returns OUT_OF_MEMORY when the target ring is full. */
struct __SYS_USER_POSTMESSAGE_V1 {
    uint32_t hwnd;
    uint32_t message;
    uint32_t wParam;
    uint32_t lParam;
};

constexpr size_t   __SYS_USER_POSTMESSAGE_V1_SIZE = sizeof(struct __SYS_USER_POSTMESSAGE_V1);
constexpr uint32_t __SYS_USER_POSTMESSAGE_V1_CALLNUMBER = 411;

