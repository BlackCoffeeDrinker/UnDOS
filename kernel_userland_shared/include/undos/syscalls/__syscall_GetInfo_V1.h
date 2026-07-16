#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Retrieves information about a running or exited process. */
struct __SYS_PROC_GETINFO_V1 {
    /** Process ID to query. */
    uint32_t pid;
    /** Command line the process was started with (query-length-then-fill). */
    char* commandLine;
    uint32_t commandLineLength;
    /** Exit code of the process, if it has exited. */
    uint32_t exitCode;
};

constexpr size_t   __SYS_PROC_GETINFO_V1_SIZE = sizeof(struct __SYS_PROC_GETINFO_V1);
constexpr uint32_t __SYS_PROC_GETINFO_V1_CALLNUMBER = 201;

