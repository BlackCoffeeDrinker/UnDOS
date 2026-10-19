#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Returns a DC clipped to the visible invalid client, origin at the client.
 * Only one paint DC per window; EndPaint must match its hwnd/DC pair.
 */
struct __SYS_USER_BEGINPAINT_V1 {
    uint32_t hwnd;
    uint32_t dc;
    struct PaintInfo paint;
};

constexpr size_t   __SYS_USER_BEGINPAINT_V1_SIZE = sizeof(struct __SYS_USER_BEGINPAINT_V1);
constexpr uint32_t __SYS_USER_BEGINPAINT_V1_CALLNUMBER = 407;

