#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_CREATEBITMAP_V1 {
    uint32_t width;
    uint32_t height;
    enum PixelFormat format;
    const uint8_t* pixels;
    uint32_t pixelsLength;
    uint32_t handle;
};

constexpr size_t   __SYS_GFX_CREATEBITMAP_V1_SIZE = sizeof(struct __SYS_GFX_CREATEBITMAP_V1);
constexpr uint32_t __SYS_GFX_CREATEBITMAP_V1_CALLNUMBER = 306;

