#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct DisplayState {
    /** Zero before activation or after an output-enable failure. */
    uint32_t active;
    /** Changes when a new mode is programmed, including enable failure. */
    uint32_t generation;
    /** All zero before the first mode is programmed. */
    struct DisplayMode mode;
};
