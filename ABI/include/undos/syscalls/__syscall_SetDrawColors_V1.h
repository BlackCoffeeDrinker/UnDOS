#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

struct __SYS_GFX_SETDRAWCOLORS_V1 {
    uint32_t dc;
    COLORREF foreground;
    COLORREF background;
    uint32_t opaque;
};

constexpr size_t   __SYS_GFX_SETDRAWCOLORS_V1_SIZE = sizeof(struct __SYS_GFX_SETDRAWCOLORS_V1);
constexpr uint32_t __SYS_GFX_SETDRAWCOLORS_V1_CALLNUMBER = 344;

