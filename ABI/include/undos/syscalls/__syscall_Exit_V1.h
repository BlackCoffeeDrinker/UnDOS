#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Terminates the current process with the given return code. */
struct __SYS_PROC_EXIT_V1 {
    /** Return code to report to the parent process. */
    uint32_t exitCode;
};

constexpr size_t   __SYS_PROC_EXIT_V1_SIZE = sizeof(struct __SYS_PROC_EXIT_V1);
constexpr uint32_t __SYS_PROC_EXIT_V1_CALLNUMBER = 202;

