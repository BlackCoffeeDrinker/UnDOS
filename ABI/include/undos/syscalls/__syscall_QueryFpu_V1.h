#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Reports usable FPU support, not merely the presence of an x87 unit.
 * Native is only valid when the kernel initializes and preserves each thread's
 * FPU state; Emulated is only valid when an instruction emulator is active.
 */
struct __SYS_ABI_QUERYFPU_V1 {
    enum FpuCapability capability;
};

constexpr size_t   __SYS_ABI_QUERYFPU_V1_SIZE = sizeof(struct __SYS_ABI_QUERYFPU_V1);
constexpr uint32_t __SYS_ABI_QUERYFPU_V1_CALLNUMBER = 207;

