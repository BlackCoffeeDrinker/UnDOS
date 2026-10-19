#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** FPU capability reported by QueryFpu. */
enum FpuCapability {
    /** Native x87 execution is not safely managed by the kernel. */
    FpuCapability_Unavailable = 0,
    /** Native x87 execution is supported with per-thread state preservation. */
    FpuCapability_Native = 1,
    /** Native x87 execution is provided by a kernel emulator. */
    FpuCapability_Emulated = 2,
};
