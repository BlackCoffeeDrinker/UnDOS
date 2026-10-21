#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

enum DCInitFlags : uint32_t {
    DCInitFlags_Primary = 0x00000001,
    DCInitFlags_Memory = 0x00000002,
    DCInitFlags_Offscreen = 0x00000004,
    DCInitFlags_NoAccel = 0x00000008,
};
