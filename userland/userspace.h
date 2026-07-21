#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls.h>

UNDOS_SYSCALL_DEF uint32_t UD_SYS_PrintV1(const char* message)
{
    uint32_t ret = 0;
    struct __SYS_DEBUG_PRINT_V1 __wire;
    __wire.message = message;
    __wire.messageLength = 0;
     while (message[__wire.messageLength]) __wire.messageLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(100), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Creates a new process from the executable at `path`. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateProcessV1(const char* path, const char* argument, uint32_t flags)
{
    uint32_t ret = 0;
    struct __SYS_PROC_CREATEPROCESS_V1 __wire;
    __wire.path = path;
    __wire.pathLength = 0;
     while (path[__wire.pathLength]) __wire.pathLength++;
    __wire.argument = argument;
    __wire.argumentLength = 0;
     while (argument[__wire.argumentLength]) __wire.argumentLength++;
    __wire.flags = flags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(200), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Retrieves information about a running or exited process. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetInfoV1(uint32_t pid, char* commandLine, uint32_t* commandLineLength, uint32_t* exitCode)
{
    uint32_t ret = 0;
    struct __SYS_PROC_GETINFO_V1 __wire;
    __wire.pid = pid;
    __wire.commandLine = commandLine;
    __wire.commandLineLength = commandLineLength ? *commandLineLength : 0;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(201), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    if (commandLineLength) *commandLineLength = __wire.commandLineLength;
    *exitCode = __wire.exitCode;
    return ret;
}

/** Terminates the current process with the given return code. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ExitV1(uint32_t exitCode)
{
    uint32_t ret = 0;
    struct __SYS_PROC_EXIT_V1 __wire;
    __wire.exitCode = exitCode;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(202), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Allocates `size` bytes of pages with the given protection. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_AllocatePagesV1(uint32_t size, enum MemoryProtectFlags flags, uint32_t* address)
{
    uint32_t ret = 0;
    struct __SYS_MEM_ALLOCATEPAGES_V1 __wire;
    __wire.size = size;
    __wire.flags = flags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(210), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *address = __wire.address;
    return ret;
}

/** Frees a previously allocated page range. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_FreePagesV1(uint32_t address, uint32_t size)
{
    uint32_t ret = 0;
    struct __SYS_MEM_FREEPAGES_V1 __wire;
    __wire.address = address;
    __wire.size = size;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(211), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Changes the protection of an existing page range (e.g. mark executable). */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ProtectV1(uint32_t address, uint32_t size, enum MemoryProtectFlags flags)
{
    uint32_t ret = 0;
    struct __SYS_MEM_PROTECT_V1 __wire;
    __wire.address = address;
    __wire.size = size;
    __wire.flags = flags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(212), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Reads data from an open file descriptor into `data`. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ReadV1(uint32_t fileDescriptor, uint8_t* data, uint32_t* dataLength)
{
    uint32_t ret = 0;
    struct __SYS_FS_READ_V1 __wire;
    __wire.fileDescriptor = fileDescriptor;
    __wire.data = data;
    __wire.dataLength = dataLength ? *dataLength : 0;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(222), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    /* buffer was mapped and filled by the kernel directly; report actual length written */
    if (dataLength) *dataLength = __wire.dataLength;
    return ret;
}

/** Writes data from `data` to an open file descriptor. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_WriteV1(uint32_t fileDescriptor, const uint8_t* data, uint32_t dataLength)
{
    uint32_t ret = 0;
    struct __SYS_FS_WRITE_V1 __wire;
    __wire.fileDescriptor = fileDescriptor;
    __wire.data = data;
    __wire.dataLength = dataLength;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(223), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

