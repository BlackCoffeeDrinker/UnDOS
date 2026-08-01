// Generated from UnDOS IDL

// See (tools/create-syscalls.py)
#pragma once

#include <stdint.h>
#include <kernel/memory/borrowed_ptr.hpp>
#include <string_view.hpp>
#include "stdkrn.hpp"
#include "__kernel_types.h"

SyscallReturnCode handle_Print_V1(__KRN_DEBUG_PRINT_V1 && args);
/** Creates a new process from the executable at `path`. */
SyscallReturnCode handle_CreateProcess_V1(__KRN_PROC_CREATEPROCESS_V1 && args);
/** Retrieves information about a running or exited process. */
SyscallReturnCode handle_GetInfo_V1(__KRN_PROC_GETINFO_V1 && args);
/** Terminates the current process with the given return code. */
SyscallReturnCode handle_Exit_V1(__KRN_PROC_EXIT_V1 && args);
/** Allocates `size` bytes of pages with the given protection. */
SyscallReturnCode handle_AllocatePages_V1(__KRN_MEM_ALLOCATEPAGES_V1 && args);
/** Frees a previously allocated page range. */
SyscallReturnCode handle_FreePages_V1(__KRN_MEM_FREEPAGES_V1 && args);
/** Changes the protection of an existing page range (e.g. mark executable). */
SyscallReturnCode handle_Protect_V1(__KRN_MEM_PROTECT_V1 && args);
SyscallReturnCode handle_Open_V1(__KRN_FS_OPEN_V1 && args);
SyscallReturnCode handle_Close_V1(__KRN_FS_CLOSE_V1 && args);
/** Reads data from an open file descriptor into `data`. */
SyscallReturnCode handle_Read_V1(__KRN_FS_READ_V1 && args);
/** Writes data from `data` to an open file descriptor. */
SyscallReturnCode handle_Write_V1(__KRN_FS_WRITE_V1 && args);

