#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/** Registers a process-scoped window class; wndProc is opaque and called by userland. */
struct __SYS_USER_REGISTERCLASS_V1 {
    const char* name;
    size_t nameLength;
    uint32_t wndProc;
    COLORREF background;
};

constexpr size_t   __SYS_USER_REGISTERCLASS_V1_SIZE = sizeof(struct __SYS_USER_REGISTERCLASS_V1);
constexpr uint32_t __SYS_USER_REGISTERCLASS_V1_CALLNUMBER = 400;

