#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Static capabilities of one gamepad or analog joystick. */
struct IoctlGamepadCaps {
    /** Number of axes the device actually reports. */
    uint32_t axisCount;
    /** Number of buttons the device actually reports. */
    uint32_t buttonCount;
    /** Non-zero when the device has a d-pad / POV hat. */
    uint32_t hasHat;
};
