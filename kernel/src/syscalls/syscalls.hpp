// Generated from UnDOS IDL

#pragma once

#include <stdint.h>
#include <kernel/memory/borrowed_ptr.hpp>
#include <string_view.hpp>
#include "stdkrn.hpp"
#include "__kernel_types.h"

/** Creates a new process from the executable at `path`. */
uint32_t handle_CreateProcess_V1(__KRN_PROC_CREATEPROCESS_V1&& args, uint32_t flags);
/** Retrieves information about a running or exited process. */
uint32_t handle_GetInfo_V1(__KRN_PROC_GETINFO_V1&& args, uint32_t flags);
/** Terminates the current process with the given return code. */
uint32_t handle_Exit_V1(__KRN_PROC_EXIT_V1&& args, uint32_t flags);
/** Allocates `size` bytes of pages with the given protection. */
uint32_t handle_AllocatePages_V1(__KRN_MEM_ALLOCATEPAGES_V1&& args, uint32_t flags);
/** Frees a previously allocated page range. */
uint32_t handle_FreePages_V1(__KRN_MEM_FREEPAGES_V1&& args, uint32_t flags);
/** Changes the protection of an existing page range (e.g. mark executable). */
uint32_t handle_Protect_V1(__KRN_MEM_PROTECT_V1&& args, uint32_t flags);
/** Reads data from an open file descriptor into `data`. */
uint32_t handle_Read_V1(__KRN_FS_READ_V1&& args, uint32_t flags);
/** Writes data from `data` to an open file descriptor. */
uint32_t handle_Write_V1(__KRN_FS_WRITE_V1&& args, uint32_t flags);

