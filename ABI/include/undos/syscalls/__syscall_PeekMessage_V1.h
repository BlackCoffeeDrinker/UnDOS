#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Non-blocking; returns TIMEOUT when the queue is empty. remove == 0 leaves it queued. */
struct __SYS_USER_PEEKMESSAGE_V1 {
    struct Msg msg;
    uint32_t remove;
};

constexpr size_t   __SYS_USER_PEEKMESSAGE_V1_SIZE = sizeof(struct __SYS_USER_PEEKMESSAGE_V1);
constexpr uint32_t __SYS_USER_PEEKMESSAGE_V1_CALLNUMBER = 410;

