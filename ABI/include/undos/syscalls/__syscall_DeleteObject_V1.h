#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

struct __SYS_GFX_DELETEOBJECT_V1 {
    HANDLER object;
};

constexpr size_t   __SYS_GFX_DELETEOBJECT_V1_SIZE = sizeof(struct __SYS_GFX_DELETEOBJECT_V1);
constexpr uint32_t __SYS_GFX_DELETEOBJECT_V1_CALLNUMBER = 307;

