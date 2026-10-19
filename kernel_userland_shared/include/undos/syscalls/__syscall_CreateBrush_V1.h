#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

struct __SYS_GFX_CREATEBRUSH_V1 {
    enum BrushPattern pattern;
    COLORREF color;
};

constexpr size_t   __SYS_GFX_CREATEBRUSH_V1_SIZE = sizeof(struct __SYS_GFX_CREATEBRUSH_V1);
constexpr uint32_t __SYS_GFX_CREATEBRUSH_V1_CALLNUMBER = 304;

