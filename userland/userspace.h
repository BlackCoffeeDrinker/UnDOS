#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls.h>

/** Creates a new process from the executable at `path`. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateProcessV1(const char* path, const char* argument, uint32_t flags)
{
    uint32_t ret = 0;
    uint32_t __len_path = 0; while (path[__len_path]) __len_path++;
    uint32_t __len_argument = 0; while (argument[__len_argument]) __len_argument++;
    const uint32_t __size = sizeof(struct __SYS_PROC_CREATEPROCESS_V1) + __len_path + 1 + __len_argument + 1;
    uint8_t __buf[__size];
    struct __SYS_PROC_CREATEPROCESS_V1* __wire = (struct __SYS_PROC_CREATEPROCESS_V1*)__buf;
    uint32_t __offset = sizeof(struct __SYS_PROC_CREATEPROCESS_V1);
    for (uint32_t __i = 0; __i <= __len_path; __i++) __buf[__offset + __i] = (path)[__i];
    __wire->path = (const char*)(__buf + __offset);
    __wire->pathLength = __len_path;
    __offset += __len_path + 1;
    for (uint32_t __i = 0; __i <= __len_argument; __i++) __buf[__offset + __i] = (argument)[__i];
    __wire->argument = (const char*)(__buf + __offset);
    __wire->argumentLength = __len_argument;
    __offset += __len_argument + 1;
    __wire->flags = flags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(200), "b"(__size), "c"(__wire), "d"(0)
        : "memory");

    return ret;
}

/** Retrieves information about a running or exited process. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetInfoV1(uint32_t pid, char* commandLine, uint32_t* commandLineLength, uint32_t* exitCode)
{
    uint32_t ret = 0;
    struct __SYS_PROC_GETINFO_V1 __wire_storage;
    struct __SYS_PROC_GETINFO_V1* __wire = &__wire_storage;
    __wire->pid = pid;
    __wire->commandLine = commandLine;
    __wire->commandLineLength = commandLineLength ? *commandLineLength : 0;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(201), "b"(sizeof(struct __SYS_PROC_GETINFO_V1)), "c"(__wire), "d"(0)
        : "memory");

    if (commandLineLength) *commandLineLength = __wire->commandLineLength;
    *exitCode = __wire->exitCode;
    return ret;
}

/** Terminates the current process with the given return code. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ExitV1(uint32_t exitCode)
{
    uint32_t ret = 0;
    struct __SYS_PROC_EXIT_V1 __wire_storage;
    struct __SYS_PROC_EXIT_V1* __wire = &__wire_storage;
    __wire->exitCode = exitCode;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(202), "b"(sizeof(struct __SYS_PROC_EXIT_V1)), "c"(__wire), "d"(0)
        : "memory");

    return ret;
}

/** Allocates `size` bytes of pages with the given protection. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_AllocatePagesV1(uint32_t size, enum MemoryProtectFlags flags, uint32_t* address)
{
    uint32_t ret = 0;
    struct __SYS_MEM_ALLOCATEPAGES_V1 __wire_storage;
    struct __SYS_MEM_ALLOCATEPAGES_V1* __wire = &__wire_storage;
    __wire->size = size;
    __wire->flags = flags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(210), "b"(sizeof(struct __SYS_MEM_ALLOCATEPAGES_V1)), "c"(__wire), "d"(0)
        : "memory");

    *address = __wire->address;
    return ret;
}

/** Frees a previously allocated page range. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_FreePagesV1(uint32_t address, uint32_t size)
{
    uint32_t ret = 0;
    struct __SYS_MEM_FREEPAGES_V1 __wire_storage;
    struct __SYS_MEM_FREEPAGES_V1* __wire = &__wire_storage;
    __wire->address = address;
    __wire->size = size;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(211), "b"(sizeof(struct __SYS_MEM_FREEPAGES_V1)), "c"(__wire), "d"(0)
        : "memory");

    return ret;
}

/** Changes the protection of an existing page range (e.g. mark executable). */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ProtectV1(uint32_t address, uint32_t size, enum MemoryProtectFlags flags)
{
    uint32_t ret = 0;
    struct __SYS_MEM_PROTECT_V1 __wire_storage;
    struct __SYS_MEM_PROTECT_V1* __wire = &__wire_storage;
    __wire->address = address;
    __wire->size = size;
    __wire->flags = flags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(212), "b"(sizeof(struct __SYS_MEM_PROTECT_V1)), "c"(__wire), "d"(0)
        : "memory");

    return ret;
}

/** Reads data from an open file descriptor into `data`. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ReadV1(uint32_t fileDescriptor, uint8_t* data, uint32_t* dataLength)
{
    uint32_t ret = 0;
    struct __SYS_FS_READ_V1 __wire_storage;
    struct __SYS_FS_READ_V1* __wire = &__wire_storage;
    __wire->fileDescriptor = fileDescriptor;
    __wire->data = data;
    __wire->dataLength = dataLength ? *dataLength : 0;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(222), "b"(sizeof(struct __SYS_FS_READ_V1)), "c"(__wire), "d"(0)
        : "memory");

    /* buffer was mapped and filled by the kernel directly; report actual length written */
    if (dataLength) *dataLength = __wire->dataLength;
    return ret;
}

/** Writes data from `data` to an open file descriptor. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_WriteV1(uint32_t fileDescriptor, const uint8_t* data, uint32_t dataLength)
{
    uint32_t ret = 0;
    struct __SYS_FS_WRITE_V1 __wire_storage;
    struct __SYS_FS_WRITE_V1* __wire = &__wire_storage;
    __wire->fileDescriptor = fileDescriptor;
    __wire->data = data;
    __wire->dataLength = dataLength;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(223), "b"(sizeof(struct __SYS_FS_WRITE_V1)), "c"(__wire), "d"(0)
        : "memory");

    return ret;
}

