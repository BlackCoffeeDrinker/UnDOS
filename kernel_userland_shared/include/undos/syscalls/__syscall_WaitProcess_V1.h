#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Blocks until a process exits and returns its exit code. Returns TIMEOUT on expiry. */
struct __SYS_PROC_WAITPROCESS_V1 {
    /** Process handle from CreateProcess. */
    uint32_t handle;
    /** Timeout in milliseconds; 0 polls, WaitTimeout_Infinite never expires. */
    uint32_t timeoutMs;
    /** Exit code of the process. */
    uint32_t exitCode;
};

constexpr size_t   __SYS_PROC_WAITPROCESS_V1_SIZE = sizeof(struct __SYS_PROC_WAITPROCESS_V1);
constexpr uint32_t __SYS_PROC_WAITPROCESS_V1_CALLNUMBER = 204;

