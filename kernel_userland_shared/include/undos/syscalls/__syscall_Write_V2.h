#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/** Writes bytes and reports short writes. V1 remains available unchanged. */
struct __SYS_IO_WRITE_V2 {
    /** Open handle with write access. */
    HANDLE handle;
    /** Bytes to write, at most 4096 per call. Use null with length zero for an empty write. */
    const uint8_t* data;
    uint32_t dataLength;
    /**
     * Actual bytes accepted, at most dataLength. Zero on handler failure;
     * undefined if dispatcher validation rejects the call before the handler.
     */
    uint32_t transferred;
};

constexpr size_t   __SYS_IO_WRITE_V2_SIZE = sizeof(struct __SYS_IO_WRITE_V2);
constexpr uint32_t __SYS_IO_WRITE_V2_CALLNUMBER = 226;

