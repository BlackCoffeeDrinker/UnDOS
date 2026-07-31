#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

enum SyscallReturnCode {
    SyscallReturnCode_OKAY = 0,
    SyscallReturnCode_UNKNOWN = 4294967295,
    SyscallReturnCode_INVALID_SIZE = 4294967294,
    SyscallReturnCode_OUT_OF_MEMORY = 4294967293,
    SyscallReturnCode_INVALID_ADDRESS = 4294967292,
    SyscallReturnCode_INVALID_ARGUMENT = 4294967291,
};
