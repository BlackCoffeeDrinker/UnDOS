#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_CREATEFONT_V1 {
    int32_t height;
    uint32_t weight;
    uint32_t charset;
    const char* name;
    size_t nameLength;
};

constexpr size_t   __SYS_GFX_CREATEFONT_V1_SIZE = sizeof(struct __SYS_GFX_CREATEFONT_V1);
constexpr uint32_t __SYS_GFX_CREATEFONT_V1_CALLNUMBER = 305;

