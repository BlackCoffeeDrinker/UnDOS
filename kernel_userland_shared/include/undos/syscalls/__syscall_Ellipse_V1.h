#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

struct __SYS_GFX_ELLIPSE_V1 {
    HANDLER dc;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

constexpr size_t   __SYS_GFX_ELLIPSE_V1_SIZE = sizeof(struct __SYS_GFX_ELLIPSE_V1);
constexpr uint32_t __SYS_GFX_ELLIPSE_V1_CALLNUMBER = 313;

