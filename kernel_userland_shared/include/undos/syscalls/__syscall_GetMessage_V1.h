#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Blocks until a message arrives; msg.message == Quit ends a message loop. */
struct __SYS_USER_GETMESSAGE_V1 {
    struct Msg msg;
};

constexpr size_t   __SYS_USER_GETMESSAGE_V1_SIZE = sizeof(struct __SYS_USER_GETMESSAGE_V1);
constexpr uint32_t __SYS_USER_GETMESSAGE_V1_CALLNUMBER = 409;

