#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Creates a new process from the executable at `path`. */
struct __SYS_PROC_CREATEPROCESS_V1 {
    /** Path to the executable to load. */
    const char* path;
    size_t pathLength;
    /** Command-line argument string passed to the new process. */
    const char* argument;
    size_t argumentLength;
    /** Combination of ProcessFlags controlling the new process. */
    uint32_t flags;
};

constexpr size_t   __SYS_PROC_CREATEPROCESS_V1_SIZE = sizeof(struct __SYS_PROC_CREATEPROCESS_V1);
constexpr uint32_t __SYS_PROC_CREATEPROCESS_V1_CALLNUMBER = 200;

