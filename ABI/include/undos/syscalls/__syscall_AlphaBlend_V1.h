#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

struct __SYS_GFX_ALPHABLEND_V1 {
    HANDLER dstDC;
    HANDLER srcDC;
    struct Rect dstRect;
    struct Rect srcRect;
    uint8_t alpha;
};

constexpr size_t   __SYS_GFX_ALPHABLEND_V1_SIZE = sizeof(struct __SYS_GFX_ALPHABLEND_V1);
constexpr uint32_t __SYS_GFX_ALPHABLEND_V1_CALLNUMBER = 322;

