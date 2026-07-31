#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

enum FileOpenMode : uint32_t {
    FileOpenMode_Read = 0x00000001,
    FileOpenMode_Write = 0x00000002,
    FileOpenMode_Append = 0x00000004,
    FileOpenMode_Create = 0x00000008,
    FileOpenMode_Truncate = 0x00000010,
    FileOpenMode_Exclusive = 0x00000020,
};
