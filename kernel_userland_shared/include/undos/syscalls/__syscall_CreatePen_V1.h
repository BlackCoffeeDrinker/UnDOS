#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

struct __SYS_GFX_CREATEPEN_V1 {
    enum PenStyle style;
    uint32_t width;
    COLORREF color;
};

constexpr size_t   __SYS_GFX_CREATEPEN_V1_SIZE = sizeof(struct __SYS_GFX_CREATEPEN_V1);
constexpr uint32_t __SYS_GFX_CREATEPEN_V1_CALLNUMBER = 303;

