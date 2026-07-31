#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_CREATEPEN_V1 {
    enum PenStyle style;
    uint32_t width;
    uint32_t color;
};

constexpr size_t   __SYS_GFX_CREATEPEN_V1_SIZE = sizeof(struct __SYS_GFX_CREATEPEN_V1);
constexpr uint32_t __SYS_GFX_CREATEPEN_V1_CALLNUMBER = 303;

