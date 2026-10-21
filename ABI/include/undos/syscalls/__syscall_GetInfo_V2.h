#pragma once

// Generated from UnDOS IDL

#include <stdint.h>

struct __SYS_PROC_GETINFO_V2 {
    struct ProcessInfo info;
};

constexpr size_t   __SYS_PROC_GETINFO_V2_SIZE = sizeof(struct __SYS_PROC_GETINFO_V2);
constexpr uint32_t __SYS_PROC_GETINFO_V2_CALLNUMBER = 201;

