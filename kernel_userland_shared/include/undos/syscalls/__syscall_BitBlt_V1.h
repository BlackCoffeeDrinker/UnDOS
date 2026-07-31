#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_BITBLT_V1 {
    uint32_t dstDC;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t srcDC;
    int32_t srcX;
    int32_t srcY;
    enum RasterOp rop;
};

constexpr size_t   __SYS_GFX_BITBLT_V1_SIZE = sizeof(struct __SYS_GFX_BITBLT_V1);
constexpr uint32_t __SYS_GFX_BITBLT_V1_CALLNUMBER = 320;

