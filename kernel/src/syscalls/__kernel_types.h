// Generated from UnDOS IDL

#pragma once

#include <stdint.h>
#include <kernel/memory/borrowed_ptr.hpp>
#include <string_view.hpp>
#include <undos/syscalls/__enum_ProcessFlags.h>
#include <undos/syscalls/__enum_MemoryProtectFlags.h>


/** Creates a new process from the executable at `path`. */
struct __KRN_PROC_CREATEPROCESS_V1 {
    /** Path to the executable to load. */
    kstd::string_view path {};
    /** Command-line argument string passed to the new process. */
    kstd::string_view argument {};
    /** Combination of ProcessFlags controlling the new process. */
    uint32_t flags;
};

/** Retrieves information about a running or exited process. */
struct __KRN_PROC_GETINFO_V1 {
    /** Process ID to query. */
    uint32_t pid;
    /** Command line the process was started with (query-length-then-fill). */
    kernel::borrowed_ptr<char> commandLine = nullptr;
    size_t commandLine_len = 0;
    /** Exit code of the process, if it has exited. */
    uint32_t exitCode;
};

/** Terminates the current process with the given return code. */
struct __KRN_PROC_EXIT_V1 {
    /** Return code to report to the parent process. */
    uint32_t exitCode;
};

/** Allocates `size` bytes of pages with the given protection. */
struct __KRN_MEM_ALLOCATEPAGES_V1 {
    /** Number of bytes to allocate (rounded up to a page boundary). */
    uint32_t size;
    /** Protection to apply to the newly allocated pages. */
    enum MemoryProtectFlags flags;
    /** Base address of the allocated region. */
    uint32_t address;
};

/** Frees a previously allocated page range. */
struct __KRN_MEM_FREEPAGES_V1 {
    /** Base address of the region to free. */
    uint32_t address;
    /** Size in bytes of the region to free. */
    uint32_t size;
};

/** Changes the protection of an existing page range (e.g. mark executable). */
struct __KRN_MEM_PROTECT_V1 {
    /** Base address of the region to reprotect. */
    uint32_t address;
    /** Size in bytes of the region to reprotect. */
    uint32_t size;
    /** New protection to apply. */
    enum MemoryProtectFlags flags;
};

/** Reads data from an open file descriptor into `data`. */
struct __KRN_FS_READ_V1 {
    /** Open file descriptor to read from. */
    uint32_t fileDescriptor;
    /** Buffer that receives the data read from the file. */
    kernel::borrowed_ptr<uint8_t> data = nullptr;
    size_t data_len = 0;
};

/** Writes data from `data` to an open file descriptor. */
struct __KRN_FS_WRITE_V1 {
    /** Open file descriptor to write to. */
    uint32_t fileDescriptor;
    /** Buffer containing the data to write to the file. */
    kernel::borrowed_ptr<uint8_t> data = nullptr;
    size_t data_len = 0;
};

