#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Identifies one of the calling process's standard streams. */
enum StdHandleSelector {
    StdHandleSelector_Stdin = 0,
    StdHandleSelector_Stdout = 1,
    StdHandleSelector_Stderr = 2,
};
