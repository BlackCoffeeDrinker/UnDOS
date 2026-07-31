#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_GETBITMAPBITS_V1 {
    uint32_t bitmap;
    uint8_t* buffer;
    uint32_t bufferLength;
};

constexpr size_t   __SYS_GFX_GETBITMAPBITS_V1_SIZE = sizeof(struct __SYS_GFX_GETBITMAPBITS_V1);
constexpr uint32_t __SYS_GFX_GETBITMAPBITS_V1_CALLNUMBER = 341;

