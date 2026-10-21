#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/** Creates an event object. */
struct __SYS_PROC_CREATEEVENT_V1 {
    /** Combination of EventFlags. */
    enum EventFlags flags;
    /** Handle to the new event. */
    HANDLE handle;
};

constexpr size_t   __SYS_PROC_CREATEEVENT_V1_SIZE = sizeof(struct __SYS_PROC_CREATEEVENT_V1);
constexpr uint32_t __SYS_PROC_CREATEEVENT_V1_CALLNUMBER = 205;

