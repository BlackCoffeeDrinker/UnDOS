#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

struct __SYS_GFX_SELECTCLIPREGION_V1 {
    HANDLER dc;
    HANDLER region;
};

constexpr size_t   __SYS_GFX_SELECTCLIPREGION_V1_SIZE = sizeof(struct __SYS_GFX_SELECTCLIPREGION_V1);
constexpr uint32_t __SYS_GFX_SELECTCLIPREGION_V1_CALLNUMBER = 331;

