#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/**
 * Creates a new process from the executable at `path`. Waits only for loader
 * mapping/dependency/relocation completion, not constructors or child exit.
 * There is no timeout: the bounded loader must signal readiness or exit on
 * failure, so failure never leaves a running child without an owned handle.
 * On failure no process handle is returned.
 * Successful handles may be closed immediately without terminating the child.
 */
struct __SYS_PROC_CREATEPROCESS_V1 {
    /** Nonempty executable path, at most 255 bytes; becomes argv[0] verbatim. */
    const char* path;
    size_t pathLength;
    /**
     * At most 1023 bytes, split on ASCII whitespace outside double quotes.
     * Quotes are removed, adjacent fragments concatenate, "" is an empty arg.
     * Backslashes are literal; unmatched quotes/NULs reject. Max argc is 32.
     */
    const char* argument;
    size_t argumentLength;
    /** Only ProcessFlags_None (zero) is supported; all other flags reject. */
    uint32_t flags;
    /** Handle to the new process (waitable with WaitProcess). */
    HANDLE handle;
};

constexpr size_t   __SYS_PROC_CREATEPROCESS_V1_SIZE = sizeof(struct __SYS_PROC_CREATEPROCESS_V1);
constexpr uint32_t __SYS_PROC_CREATEPROCESS_V1_CALLNUMBER = 200;

