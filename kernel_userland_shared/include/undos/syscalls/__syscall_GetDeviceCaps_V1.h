#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

struct __SYS_GFX_GETDEVICECAPS_V1 {
    HANDLER dc;
    struct DeviceCaps caps;
};

constexpr size_t   __SYS_GFX_GETDEVICECAPS_V1_SIZE = sizeof(struct __SYS_GFX_GETDEVICECAPS_V1);
constexpr uint32_t __SYS_GFX_GETDEVICECAPS_V1_CALLNUMBER = 343;

