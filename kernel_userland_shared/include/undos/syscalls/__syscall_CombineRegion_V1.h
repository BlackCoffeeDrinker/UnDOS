#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_COMBINEREGION_V1 {
    uint32_t dest;
    uint32_t src1;
    uint32_t src2;
    enum RegionCombineMode mode;
};

constexpr size_t   __SYS_GFX_COMBINEREGION_V1_SIZE = sizeof(struct __SYS_GFX_COMBINEREGION_V1);
constexpr uint32_t __SYS_GFX_COMBINEREGION_V1_CALLNUMBER = 332;

