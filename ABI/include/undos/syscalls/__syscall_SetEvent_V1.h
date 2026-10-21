#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/** Signals an event. */
struct __SYS_PROC_SETEVENT_V1 {
    /** Event handle. */
    HANDLE handle;
};

constexpr size_t   __SYS_PROC_SETEVENT_V1_SIZE = sizeof(struct __SYS_PROC_SETEVENT_V1);
constexpr uint32_t __SYS_PROC_SETEVENT_V1_CALLNUMBER = 206;

