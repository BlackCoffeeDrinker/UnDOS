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
SyscallReturnCode handle_SetDrawColors_V1(__KRN_GFX_SETDRAWCOLORS_V1 && args);
SyscallReturnCode handle_QueryFontEngine_V1(__KRN_GFX_QUERYFONTENGINE_V1 && args);
SyscallReturnCode handle_BeginFontUpload_V1(__KRN_GFX_BEGINFONTUPLOAD_V1 && args);
SyscallReturnCode handle_AppendFontUpload_V1(__KRN_GFX_APPENDFONTUPLOAD_V1 && args);
SyscallReturnCode handle_CommitFontUpload_V1(__KRN_GFX_COMMITFONTUPLOAD_V1 && args);
SyscallReturnCode handle_CloseFontResource_V1(__KRN_GFX_CLOSEFONTRESOURCE_V1 && args);
/** Registers a process-scoped window class; wndProc is opaque and called by userland. */
SyscallReturnCode handle_RegisterClass_V1(__KRN_USER_REGISTERCLASS_V1 && args);
/**
 * Creates a window owned by the calling thread. Child requires a same-process
 * parent; x/y are relative to its client origin. Otherwise parent must be zero.
 * Coordinates and dimensions must fit the signed 16-bit screen range.
 */
SyscallReturnCode handle_CreateWindow_V1(__KRN_USER_CREATEWINDOW_V1 && args);
/**
 * Immediately destroys the window and descendants and removes their queued
 * messages. Userland dispatches any Destroy callback before this syscall;
 * no message may subsequently target an invalidated handle.
 */
SyscallReturnCode handle_DestroyWindow_V1(__KRN_USER_DESTROYWINDOW_V1 && args);
/**
 * Hide preserves state, Show preserves state, Restore restores saved geometry.
 * Minimize creates an icon plus label for children in the parent client
 * (roots retain a desktop caption). Click selects a child icon, double-click
 * or Enter restores it; Ctrl+Tab/F6 cycles including minimized child icons.
 * Maximize fills the parent client or desktop. Parent hide/minimize suppresses
 * descendant visibility.
 */
SyscallReturnCode handle_ShowWindow_V1(__KRN_USER_SHOWWINDOW_V1 && args);
SyscallReturnCode handle_MoveWindow_V1(__KRN_USER_MOVEWINDOW_V1 && args);
/** Window rectangle in screen coordinates. */
SyscallReturnCode handle_GetWindowRect_V1(__KRN_USER_GETWINDOWRECT_V1 && args);
/** Adds rect (client coordinates) to the invalid area; whole != 0 invalidates the client. */
SyscallReturnCode handle_InvalidateRect_V1(__KRN_USER_INVALIDATERECT_V1 && args);
/**
 * Returns a DC clipped to the visible invalid client, origin at the client.
 * Only one paint DC per window; EndPaint must match its hwnd/DC pair.
 */
SyscallReturnCode handle_BeginPaint_V1(__KRN_USER_BEGINPAINT_V1 && args);
SyscallReturnCode handle_EndPaint_V1(__KRN_USER_ENDPAINT_V1 && args);
/** Blocks until a message arrives; msg.message == Quit ends a message loop. */
SyscallReturnCode handle_GetMessage_V1(__KRN_USER_GETMESSAGE_V1 && args);
/** Non-blocking; returns TIMEOUT when the queue is empty. remove == 0 leaves it queued. */
SyscallReturnCode handle_PeekMessage_V1(__KRN_USER_PEEKMESSAGE_V1 && args);
/** Queues a message; returns OUT_OF_MEMORY when the target ring is full. */
SyscallReturnCode handle_PostMessage_V1(__KRN_USER_POSTMESSAGE_V1 && args);
/** One of WindowLongIndex. */
SyscallReturnCode handle_GetWindowLongPtr_V1(__KRN_USER_GETWINDOWLONGPTR_V1 && args);
SyscallReturnCode handle_SetWindowLongPtr_V1(__KRN_USER_SETWINDOWLONGPTR_V1 && args);
SyscallReturnCode handle_SetCapture_V1(__KRN_USER_SETCAPTURE_V1 && args);
SyscallReturnCode handle_ReleaseCapture_V1(__KRN_USER_RELEASECAPTURE_V1 && args);
/** Client bounds {0,0,width,height}; empty while minimized. */
SyscallReturnCode handle_GetClientRect_V1(__KRN_USER_GETCLIENTRECT_V1 && args);
/**
 * Focus a visible same-process window and activate its root. A minimized
 * child icon may receive focus for Enter/Alt+Space; minimized roots cannot.
 * hwnd == 0 clears this process's focus without changing activation.
 * Key lParam: bits 0..7 scan code, 8..10 key_flags, 16..24 modifier_flags.
 * Char lParam carries the same metadata. Mouse wParam low bits: left=1,right=2;
 * modifier_flags occupy bits 16..24. Mouse lParam packs signed client x/y.
 */
SyscallReturnCode handle_SetFocus_V1(__KRN_USER_SETFOCUS_V1 && args);
/** Set a same-process window title (at most 63 bytes plus terminator). */
SyscallReturnCode handle_SetWindowText_V1(__KRN_USER_SETWINDOWTEXT_V1 && args);
/**
 * Reports usable FPU support, not merely the presence of an x87 unit.
 * Native is only valid when the kernel initializes and preserves each thread's
 * FPU state; Emulated is only valid when an instruction emulator is active.
 */
SyscallReturnCode handle_QueryFpu_V1(__KRN_ABI_QUERYFPU_V1 && args);
SyscallReturnCode handle_Print_V1(__KRN_DEBUG_PRINT_V1 && args);
/**
 * Creates a new process from the executable at `path`. Waits only for loader
 * mapping/dependency/relocation completion, not constructors or child exit.
 * There is no timeout: the bounded loader must signal readiness or exit on
 * failure, so failure never leaves a running child without an owned handle.
 * On failure no process handle is returned.
 * Successful handles may be closed immediately without terminating the child.
 */
SyscallReturnCode handle_CreateProcess_V1(__KRN_PROC_CREATEPROCESS_V1 && args);
/**
 * Creates a process with explicit standard streams. Same path, argument,
 * flags, readiness and handle ownership contract as CreateProcessV1.
 * Only these streams are inherited; unrelated handles are not inherited.
 * Handles are caller-local open I/O objects, stdin requires read access,
 * stdout/stderr require write access. Child handles are independent copies.
 */
SyscallReturnCode handle_CreateProcess_V2(__KRN_PROC_CREATEPROCESS_V2 && args);
/** Retrieves information about a running or exited process. */
SyscallReturnCode handle_GetInfo_V1(__KRN_PROC_GETINFO_V1 && args);
/** Terminates the current process with the given return code. */
SyscallReturnCode handle_Exit_V1(__KRN_PROC_EXIT_V1 && args);
/** Blocks until a waitable handle (event or process) is signalled. Returns TIMEOUT on expiry. */
SyscallReturnCode handle_WaitForObject_V1(__KRN_PROC_WAITFOROBJECT_V1 && args);
/** Blocks until a process exits and returns its exit code. Returns TIMEOUT on expiry. */
SyscallReturnCode handle_WaitProcess_V1(__KRN_PROC_WAITPROCESS_V1 && args);
/** Creates an event object. */
SyscallReturnCode handle_CreateEvent_V1(__KRN_PROC_CREATEEVENT_V1 && args);
/** Signals an event. */
SyscallReturnCode handle_SetEvent_V1(__KRN_PROC_SETEVENT_V1 && args);
/**
 * Allocates zeroed pages, rounding size up to 4096. Read is required; unknown
 * flags and no-access/write-only mappings reject. i386 does not enforce NX.
 */
SyscallReturnCode handle_AllocatePages_V1(__KRN_MEM_ALLOCATEPAGES_V1 && args);
/**
 * Frees a whole eager anonymous region; base must be page aligned and size,
 * rounded up to 4096, must equal the region size. No partial frees or aliases.
 */
SyscallReturnCode handle_FreePages_V1(__KRN_MEM_FREEPAGES_V1 && args);
/** Reserved: currently rejects with INVALID_ARGUMENT (no partial VAD updates). */
SyscallReturnCode handle_Protect_V1(__KRN_MEM_PROTECT_V1 && args);
/**
 * Maps zeroed anonymous user pages at a page-aligned address selected by the
 * caller. Read is required; unknown flags and no-access/write-only mappings
 * reject. i386 does not enforce NX.
 */
SyscallReturnCode handle_MapFixedPages_V1(__KRN_MEM_MAPFIXEDPAGES_V1 && args);
SyscallReturnCode handle_Open_V1(__KRN_IO_OPEN_V1 && args);
SyscallReturnCode handle_Close_V1(__KRN_IO_CLOSE_V1 && args);
/** Reads data from an open object handle into `data`. */
SyscallReturnCode handle_Read_V1(__KRN_IO_READ_V1 && args);
/** Writes data from `data` to an open object handle. */
SyscallReturnCode handle_Write_V1(__KRN_IO_WRITE_V1 && args);
/** Writes bytes and reports short writes. V1 remains available unchanged. */
SyscallReturnCode handle_Write_V2(__KRN_IO_WRITE_V2 && args);
/** Sends a control code to the device behind an open handle. */
SyscallReturnCode handle_Control_V1(__KRN_IO_CONTROL_V1 && args);
/**
 * Returns the opaque handle assigned to a standard stream, or
 * INVALID_ARGUMENT if that stream is not available to the calling process.
 */
SyscallReturnCode handle_GetStdHandle_V1(__KRN_IO_GETSTDHANDLE_V1 && args);
/** Creates a paired virtual TTY returning source and sink handles. */
SyscallReturnCode handle_Create_V1(__KRN_TTY_CREATE_V1 && args);
/** Sets TTY configuration flags and window dimensions. */
SyscallReturnCode handle_SetConfig_V1(__KRN_TTY_SETCONFIG_V1 && args);
/** Retrieves TTY configuration flags and window dimensions. */
SyscallReturnCode handle_GetConfig_V1(__KRN_TTY_GETCONFIG_V1 && args);

