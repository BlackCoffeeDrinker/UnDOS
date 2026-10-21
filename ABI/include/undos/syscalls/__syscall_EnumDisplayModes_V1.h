#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Enumerates monitor-approved primary-display modes without activating output.
 * Ordinals start at zero. OKAY with available=0 and a zero mode marks the end.
 * UNSUPPORTED means no usable primary display.
 */
struct __SYS_GFX_ENUMDISPLAYMODES_V1 {
    uint32_t index;
    struct DisplayMode mode;
    uint32_t available;
};

constexpr size_t   __SYS_GFX_ENUMDISPLAYMODES_V1_SIZE = sizeof(struct __SYS_GFX_ENUMDISPLAYMODES_V1);
constexpr uint32_t __SYS_GFX_ENUMDISPLAYMODES_V1_CALLNUMBER = 350;

