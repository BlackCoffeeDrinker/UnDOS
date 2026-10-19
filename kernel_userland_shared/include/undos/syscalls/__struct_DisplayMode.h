#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Primary-display mode. modeId is an opaque driver mode ID, not the
 * enumeration ordinal. Refresh rate is in Hz; colorDepth is bits per pixel.
 */
struct DisplayMode {
    uint32_t modeId;
    uint32_t width;
    uint32_t height;
    uint32_t colorDepth;
    uint32_t refreshHz;
};
