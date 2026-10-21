#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/**
 * Creates a process with explicit standard streams. Same path, argument,
 * flags, readiness and handle ownership contract as CreateProcessV1.
 * Only these streams are inherited; unrelated handles are not inherited.
 * Handles are caller-local open I/O objects, stdin requires read access,
 * stdout/stderr require write access. Child handles are independent copies.
 */
struct __SYS_PROC_CREATEPROCESS_V2 {
    const char* path;
    size_t pathLength;
    const char* argument;
    size_t argumentLength;
    uint32_t flags;
    HANDLE stdinHandle;
    HANDLE stdoutHandle;
    HANDLE stderrHandle;
    HANDLE handle;
};

constexpr size_t   __SYS_PROC_CREATEPROCESS_V2_SIZE = sizeof(struct __SYS_PROC_CREATEPROCESS_V2);
constexpr uint32_t __SYS_PROC_CREATEPROCESS_V2_CALLNUMBER = 208;

