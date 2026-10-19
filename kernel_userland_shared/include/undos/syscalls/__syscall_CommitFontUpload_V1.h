#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_COMMITFONTUPLOAD_V1 {
    uint32_t upload;
    uint32_t face;
    uint32_t validationError;
};

constexpr size_t   __SYS_GFX_COMMITFONTUPLOAD_V1_SIZE = sizeof(struct __SYS_GFX_COMMITFONTUPLOAD_V1);
constexpr uint32_t __SYS_GFX_COMMITFONTUPLOAD_V1_CALLNUMBER = 348;

