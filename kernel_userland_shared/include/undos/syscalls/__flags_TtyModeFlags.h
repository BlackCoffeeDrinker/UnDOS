#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

enum TtyModeFlags : uint32_t {
    TtyModeFlags_Echo = 0x00000001,
    TtyModeFlags_Canonical = 0x00000002,
    TtyModeFlags_CrToNl = 0x00000004,
    TtyModeFlags_NlToCrNl = 0x00000008,
};
