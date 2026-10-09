#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Latest axis/button snapshot of one gamepad or analog joystick. Axes are
 * normalized to -32767..32767 and centred on 0, independent of the device's
 * native resolution, and are named after their HID Generic Desktop usages.
 * The axes are spelled out one field at a time rather than as an array
 * because the IDL only supports array fields inside syscall bodies.
 */
struct IoctlGamepadState {
    /** Generic Desktop X. */
    int16_t leftX;
    /** Generic Desktop Y. */
    int16_t leftY;
    /** Generic Desktop Rx. */
    int16_t rightX;
    /** Generic Desktop Ry. */
    int16_t rightY;
    /** Generic Desktop Z. */
    int16_t leftTrigger;
    /** Generic Desktop Rz. */
    int16_t rightTrigger;
    /** Bit n is set while button n is down. */
    uint32_t buttons;
    /** D-pad / POV hat, each -1, 0 or +1. */
    int8_t hatX;
    int8_t hatY;
    /** Increments on every sample, so a reader can tell it skipped one. */
    uint32_t sequence;
};
