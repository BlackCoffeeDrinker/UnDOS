#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

enum Device3DFlags : uint32_t {
    Device3DFlags_Hardware = 0x00000001,
    Device3DFlags_Software = 0x00000002,
    Device3DFlags_VertexAccel = 0x00000004,
    Device3DFlags_TextureAccel = 0x00000008,
};
