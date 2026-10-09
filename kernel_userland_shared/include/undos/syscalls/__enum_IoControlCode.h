#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Device control codes understood by IO Control. */
enum IoControlCode {
    /** Display: output is struct<IoctlDisplayMode>. */
    IoControlCode_DisplayQueryMode = 256,
    /** Pointer: input is struct<IoctlMouseConfig>. */
    IoControlCode_MouseConfigure = 512,
    /** Gamepad: output is struct<IoctlGamepadState>. */
    IoControlCode_GamepadReadState = 768,
    /** Gamepad: output is struct<IoctlGamepadCaps>. */
    IoControlCode_GamepadQueryCaps = 769,
};
