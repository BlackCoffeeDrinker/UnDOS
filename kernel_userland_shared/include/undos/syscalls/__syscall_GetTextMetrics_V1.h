#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_GETTEXTMETRICS_V1 {
    uint32_t dc;
    struct DeviceCaps metrics;
};

constexpr size_t   __SYS_GFX_GETTEXTMETRICS_V1_SIZE = sizeof(struct __SYS_GFX_GETTEXTMETRICS_V1);
constexpr uint32_t __SYS_GFX_GETTEXTMETRICS_V1_CALLNUMBER = 340;

