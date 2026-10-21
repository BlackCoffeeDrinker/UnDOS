#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/** Sends a control code to the device behind an open handle. */
struct __SYS_IO_CONTROL_V1 {
    /** Open handle to the device. */
    HANDLE handle;
    /** One of IoControlCode. */
    uint32_t code;
    /** Code-specific input payload. */
    const uint8_t* input;
    uint32_t inputLength;
    /** Code-specific output payload; receives the number of bytes written. */
    uint8_t* output;
    uint32_t outputLength;
};

constexpr size_t   __SYS_IO_CONTROL_V1_SIZE = sizeof(struct __SYS_IO_CONTROL_V1);
constexpr uint32_t __SYS_IO_CONTROL_V1_CALLNUMBER = 224;

