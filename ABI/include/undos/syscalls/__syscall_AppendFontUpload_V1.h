#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_GFX_APPENDFONTUPLOAD_V1 {
    uint32_t upload;
    uint32_t offset;
    const uint8_t* data;
    uint32_t dataLength;
};

constexpr size_t   __SYS_GFX_APPENDFONTUPLOAD_V1_SIZE = sizeof(struct __SYS_GFX_APPENDFONTUPLOAD_V1);
constexpr uint32_t __SYS_GFX_APPENDFONTUPLOAD_V1_CALLNUMBER = 347;

