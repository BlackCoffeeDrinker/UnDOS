#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct IoctlMouseConfig {
    /** Non-zero enables data reporting, zero disables it. */
    uint32_t enable;
    /** Sample rate in reports per second (10-200), 0 keeps the current rate. */
    uint32_t sampleRate;
};
