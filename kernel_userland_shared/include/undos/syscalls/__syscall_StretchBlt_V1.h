#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_STRETCHBLT_V1 {
    uint32_t dstDC;
    int32_t x;
    int32_t y;
    uint32_t dstWidth;
    uint32_t dstHeight;
    uint32_t srcDC;
    int32_t srcX;
    int32_t srcY;
    uint32_t srcWidth;
    uint32_t srcHeight;
    enum RasterOp rop;
};

constexpr size_t   __SYS_GFX_STRETCHBLT_V1_SIZE = sizeof(struct __SYS_GFX_STRETCHBLT_V1);
constexpr uint32_t __SYS_GFX_STRETCHBLT_V1_CALLNUMBER = 321;

