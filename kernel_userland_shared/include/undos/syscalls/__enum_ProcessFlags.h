#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Flags controlling how a new process is created. */
enum ProcessFlags {
    /** No special behavior. */
    ProcessFlags_None = 0,
    /** The child process is detached from its parent (no notification on exit). */
    ProcessFlags_Detached = 1,
    /** The child process inherits the parent's environment variables. */
    ProcessFlags_InheritEnv = 2,
};
