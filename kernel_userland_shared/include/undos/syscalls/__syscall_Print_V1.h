#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct __SYS_DEBUG_PRINT_V1 {
    const char* message;
    uint32_t messageLength;
};

constexpr size_t   __SYS_DEBUG_PRINT_V1_SIZE = sizeof(struct __SYS_DEBUG_PRINT_V1);
constexpr uint32_t __SYS_DEBUG_PRINT_V1_CALLNUMBER = 100;

