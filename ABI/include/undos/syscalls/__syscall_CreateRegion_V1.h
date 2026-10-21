#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_CREATEREGION_V1 {
    const struct Rect* rects;
    size_t rectsLength;
};

constexpr size_t   __SYS_GFX_CREATEREGION_V1_SIZE = sizeof(struct __SYS_GFX_CREATEREGION_V1);
constexpr uint32_t __SYS_GFX_CREATEREGION_V1_CALLNUMBER = 330;

