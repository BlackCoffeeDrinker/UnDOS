#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_CREATEDC_V1 {
    const char* driverName;
    uint32_t driverNameLength;
    const char* deviceName;
    uint32_t deviceNameLength;
    enum DCInitFlags initFlags;
};

constexpr size_t   __SYS_GFX_CREATEDC_V1_SIZE = sizeof(struct __SYS_GFX_CREATEDC_V1);
constexpr uint32_t __SYS_GFX_CREATEDC_V1_CALLNUMBER = 300;

