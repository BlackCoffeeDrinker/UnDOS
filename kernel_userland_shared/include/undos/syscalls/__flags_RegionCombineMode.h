#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

enum RegionCombineMode : uint32_t {
    RegionCombineMode_Union = 0x00000001,
    RegionCombineMode_Intersect = 0x00000002,
    RegionCombineMode_Subtract = 0x00000004,
    RegionCombineMode_Xor = 0x00000008,
};
