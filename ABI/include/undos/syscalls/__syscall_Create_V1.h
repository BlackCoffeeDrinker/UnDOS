#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/** Creates a paired virtual TTY returning source and sink handles. */
struct __SYS_TTY_CREATE_V1 {
    HANDLE sourceHandle;
    HANDLE sinkHandle;
};

constexpr size_t   __SYS_TTY_CREATE_V1_SIZE = sizeof(struct __SYS_TTY_CREATE_V1);
constexpr uint32_t __SYS_TTY_CREATE_V1_CALLNUMBER = 250;

