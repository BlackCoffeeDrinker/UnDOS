#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Queries the primary display, including its inactive pre-graphics state.
 * Returns UNSUPPORTED when no primary display exists.
 */
struct __SYS_GFX_GETDISPLAYMODE_V1 {
    struct DisplayState state;
};

constexpr size_t   __SYS_GFX_GETDISPLAYMODE_V1_SIZE = sizeof(struct __SYS_GFX_GETDISPLAYMODE_V1);
constexpr uint32_t __SYS_GFX_GETDISPLAYMODE_V1_CALLNUMBER = 351;

