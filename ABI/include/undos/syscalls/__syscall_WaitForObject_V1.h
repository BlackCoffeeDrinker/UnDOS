#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/** Blocks until a waitable handle (event or process) is signalled. Returns TIMEOUT on expiry. */
struct __SYS_PROC_WAITFOROBJECT_V1 {
    /** Handle to wait on. */
    HANDLE handle;
    /** Timeout in milliseconds; 0 polls, WaitTimeout_Infinite never expires. */
    uint32_t timeoutMs;
};

constexpr size_t   __SYS_PROC_WAITFOROBJECT_V1_SIZE = sizeof(struct __SYS_PROC_WAITFOROBJECT_V1);
constexpr uint32_t __SYS_PROC_WAITFOROBJECT_V1_CALLNUMBER = 203;

