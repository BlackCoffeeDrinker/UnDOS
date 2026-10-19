#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

struct __SYS_GFX_DELETEDC_V1 {
    HANDLER dc;
};

constexpr size_t   __SYS_GFX_DELETEDC_V1_SIZE = sizeof(struct __SYS_GFX_DELETEDC_V1);
constexpr uint32_t __SYS_GFX_DELETEDC_V1_CALLNUMBER = 301;

