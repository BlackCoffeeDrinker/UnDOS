#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/**
 * Returns the opaque handle assigned to a standard stream, or
 * INVALID_ARGUMENT if that stream is not available to the calling process.
 */
struct __SYS_IO_GETSTDHANDLE_V1 {
    /** One of StdHandleSelector. */
    enum StdHandleSelector selector;
    /** Process-local opaque handle for the selected stream. */
    HANDLE handle;
};

constexpr size_t   __SYS_IO_GETSTDHANDLE_V1_SIZE = sizeof(struct __SYS_IO_GETSTDHANDLE_V1);
constexpr uint32_t __SYS_IO_GETSTDHANDLE_V1_CALLNUMBER = 225;

