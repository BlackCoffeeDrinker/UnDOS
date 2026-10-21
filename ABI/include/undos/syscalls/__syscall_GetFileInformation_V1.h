#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls/__aliases.h>

/** Returns information for an open VFS file/directory handle with read access. */
struct __SYS_IO_GETFILEINFORMATION_V1 {
    HANDLE handle;
    struct FileInformation information;
};

constexpr size_t   __SYS_IO_GETFILEINFORMATION_V1_SIZE = sizeof(struct __SYS_IO_GETFILEINFORMATION_V1);
constexpr uint32_t __SYS_IO_GETFILEINFORMATION_V1_CALLNUMBER = 229;

