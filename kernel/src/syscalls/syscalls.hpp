// Generated from UnDOS IDL

// See (tools/create-syscalls.py)
#pragma once

#include <stdint.h>
#include <kernel/memory/borrowed_ptr.hpp>
#include <string_view.hpp>
#include "stdkrn.hpp"
#include "__kernel_types.h"

SyscallReturnCode handle_CreateDC_V1(__KRN_GFX_CREATEDC_V1 && args);
SyscallReturnCode handle_DeleteDC_V1(__KRN_GFX_DELETEDC_V1 && args);
SyscallReturnCode handle_SelectObject_V1(__KRN_GFX_SELECTOBJECT_V1 && args);
SyscallReturnCode handle_CreatePen_V1(__KRN_GFX_CREATEPEN_V1 && args);
SyscallReturnCode handle_CreateBrush_V1(__KRN_GFX_CREATEBRUSH_V1 && args);
SyscallReturnCode handle_CreateFont_V1(__KRN_GFX_CREATEFONT_V1 && args);
SyscallReturnCode handle_CreateBitmap_V1(__KRN_GFX_CREATEBITMAP_V1 && args);
SyscallReturnCode handle_DeleteObject_V1(__KRN_GFX_DELETEOBJECT_V1 && args);
SyscallReturnCode handle_MoveTo_V1(__KRN_GFX_MOVETO_V1 && args);
SyscallReturnCode handle_LineTo_V1(__KRN_GFX_LINETO_V1 && args);
SyscallReturnCode handle_Rectangle_V1(__KRN_GFX_RECTANGLE_V1 && args);
SyscallReturnCode handle_Ellipse_V1(__KRN_GFX_ELLIPSE_V1 && args);
SyscallReturnCode handle_TextOut_V1(__KRN_GFX_TEXTOUT_V1 && args);
SyscallReturnCode handle_BitBlt_V1(__KRN_GFX_BITBLT_V1 && args);
SyscallReturnCode handle_StretchBlt_V1(__KRN_GFX_STRETCHBLT_V1 && args);
SyscallReturnCode handle_AlphaBlend_V1(__KRN_GFX_ALPHABLEND_V1 && args);
SyscallReturnCode handle_CreateRegion_V1(__KRN_GFX_CREATEREGION_V1 && args);
SyscallReturnCode handle_SelectClipRegion_V1(__KRN_GFX_SELECTCLIPREGION_V1 && args);
SyscallReturnCode handle_CombineRegion_V1(__KRN_GFX_COMBINEREGION_V1 && args);
SyscallReturnCode handle_SetViewportOrg_V1(__KRN_GFX_SETVIEWPORTORG_V1 && args);
SyscallReturnCode handle_SetWindowOrg_V1(__KRN_GFX_SETWINDOWORG_V1 && args);
SyscallReturnCode handle_GetTextMetrics_V1(__KRN_GFX_GETTEXTMETRICS_V1 && args);
SyscallReturnCode handle_GetBitmapBits_V1(__KRN_GFX_GETBITMAPBITS_V1 && args);
SyscallReturnCode handle_SetBitmapBits_V1(__KRN_GFX_SETBITMAPBITS_V1 && args);
SyscallReturnCode handle_GetDeviceCaps_V1(__KRN_GFX_GETDEVICECAPS_V1 && args);
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
SyscallReturnCode handle_Open_V1(__KRN_IO_OPEN_V1 && args);
SyscallReturnCode handle_Close_V1(__KRN_IO_CLOSE_V1 && args);
/** Reads data from an open object handle into `data`. */
SyscallReturnCode handle_Read_V1(__KRN_IO_READ_V1 && args);
/** Writes data from `data` to an open object handle. */
SyscallReturnCode handle_Write_V1(__KRN_IO_WRITE_V1 && args);
/** Creates a paired virtual TTY returning source and sink handles. */
SyscallReturnCode handle_Create_V1(__KRN_TTY_CREATE_V1 && args);
/** Sets TTY configuration flags and window dimensions. */
SyscallReturnCode handle_SetConfig_V1(__KRN_TTY_SETCONFIG_V1 && args);
/** Retrieves TTY configuration flags and window dimensions. */
SyscallReturnCode handle_GetConfig_V1(__KRN_TTY_GETCONFIG_V1 && args);

