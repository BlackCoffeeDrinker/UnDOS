// Generated from UnDOS IDL

// See (Tools/create-syscalls.py)
#pragma once

#include <stdint.h>
#include <kernel/kerror.hpp>
#include <kernel/memory/borrowed_ptr.hpp>
#include <string_view.hpp>
#include "stdkrn.hpp"
#include "__kernel_types.h"

#include <kernel/kobject/KModuleObject.hpp>

namespace kernel::syscall {
// Binds every syscall provided by `module` (its exported handle_<Name>_<Version>
// functions) into the dispatch slot table. All-or-nothing; fails with NotFound when a
// declared handler is not exported and AlreadyExists when a slot is already bound.
kstd::error_code BindProvider(const KObjectPtr<KModuleObject> &module);
}// namespace kernel::syscall

kstd::error_code handle_CreateDC_V1(__KRN_GFX_CREATEDC_V1 && args);
kstd::error_code handle_DeleteDC_V1(__KRN_GFX_DELETEDC_V1 && args);
kstd::error_code handle_SelectObject_V1(__KRN_GFX_SELECTOBJECT_V1 && args);
kstd::error_code handle_CreatePen_V1(__KRN_GFX_CREATEPEN_V1 && args);
kstd::error_code handle_CreateBrush_V1(__KRN_GFX_CREATEBRUSH_V1 && args);
kstd::error_code handle_CreateFont_V1(__KRN_GFX_CREATEFONT_V1 && args);
kstd::error_code handle_CreateBitmap_V1(__KRN_GFX_CREATEBITMAP_V1 && args);
kstd::error_code handle_DeleteObject_V1(__KRN_GFX_DELETEOBJECT_V1 && args);
kstd::error_code handle_MoveTo_V1(__KRN_GFX_MOVETO_V1 && args);
kstd::error_code handle_LineTo_V1(__KRN_GFX_LINETO_V1 && args);
kstd::error_code handle_Rectangle_V1(__KRN_GFX_RECTANGLE_V1 && args);
kstd::error_code handle_Ellipse_V1(__KRN_GFX_ELLIPSE_V1 && args);
kstd::error_code handle_TextOut_V1(__KRN_GFX_TEXTOUT_V1 && args);
kstd::error_code handle_BitBlt_V1(__KRN_GFX_BITBLT_V1 && args);
kstd::error_code handle_StretchBlt_V1(__KRN_GFX_STRETCHBLT_V1 && args);
kstd::error_code handle_AlphaBlend_V1(__KRN_GFX_ALPHABLEND_V1 && args);
kstd::error_code handle_CreateRegion_V1(__KRN_GFX_CREATEREGION_V1 && args);
kstd::error_code handle_SelectClipRegion_V1(__KRN_GFX_SELECTCLIPREGION_V1 && args);
kstd::error_code handle_CombineRegion_V1(__KRN_GFX_COMBINEREGION_V1 && args);
kstd::error_code handle_SetViewportOrg_V1(__KRN_GFX_SETVIEWPORTORG_V1 && args);
kstd::error_code handle_SetWindowOrg_V1(__KRN_GFX_SETWINDOWORG_V1 && args);
kstd::error_code handle_GetTextMetrics_V1(__KRN_GFX_GETTEXTMETRICS_V1 && args);
kstd::error_code handle_GetBitmapBits_V1(__KRN_GFX_GETBITMAPBITS_V1 && args);
kstd::error_code handle_SetBitmapBits_V1(__KRN_GFX_SETBITMAPBITS_V1 && args);
kstd::error_code handle_GetDeviceCaps_V1(__KRN_GFX_GETDEVICECAPS_V1 && args);
kstd::error_code handle_SetDrawColors_V1(__KRN_GFX_SETDRAWCOLORS_V1 && args);
kstd::error_code handle_QueryFontEngine_V1(__KRN_GFX_QUERYFONTENGINE_V1 && args);
kstd::error_code handle_BeginFontUpload_V1(__KRN_GFX_BEGINFONTUPLOAD_V1 && args);
kstd::error_code handle_AppendFontUpload_V1(__KRN_GFX_APPENDFONTUPLOAD_V1 && args);
kstd::error_code handle_CommitFontUpload_V1(__KRN_GFX_COMMITFONTUPLOAD_V1 && args);
kstd::error_code handle_CloseFontResource_V1(__KRN_GFX_CLOSEFONTRESOURCE_V1 && args);
/**
 * Enumerates monitor-approved primary-display modes without activating output.
 * Ordinals start at zero. OKAY with available=0 and a zero mode marks the end.
 * UNSUPPORTED means no usable primary display.
 */
kstd::error_code handle_EnumDisplayModes_V1(__KRN_GFX_ENUMDISPLAYMODES_V1 && args);
/**
 * Queries the primary display, including its inactive pre-graphics state.
 * Returns UNSUPPORTED when no primary display exists.
 */
kstd::error_code handle_GetDisplayMode_V1(__KRN_GFX_GETDISPLAYMODE_V1 && args);
/**
 * Activates or changes the primary display. Any process may call this.
 * modeId comes from EnumDisplayModes, not its enumeration ordinal.
 * Existing screen DC handles remain valid; pixels are lost and all windows
 * repaint. DisplayChange is coalesced per window and survives a full queue.
 * Invalid/monitor-rejected IDs fail without changing the display. Allocation
 * failures preserve the previous mode. Hardware enable failure reports UNKNOWN;
 * GetDisplayMode then reports the programmed mode as inactive.
 * This call does not persist a preference.
 */
kstd::error_code handle_SetDisplayMode_V1(__KRN_GFX_SETDISPLAYMODE_V1 && args);
/** Registers a process-scoped window class; wndProc is opaque and called by userland. */
kstd::error_code handle_RegisterClass_V1(__KRN_USER_REGISTERCLASS_V1 && args);
/**
 * Creates a window owned by the calling thread. Child requires a same-process
 * parent; x/y are relative to its client origin. Otherwise parent must be zero.
 * Coordinates and dimensions must fit the signed 16-bit screen range.
 */
kstd::error_code handle_CreateWindow_V1(__KRN_USER_CREATEWINDOW_V1 && args);
/**
 * Immediately destroys the window and descendants and removes their queued
 * messages. Userland dispatches any Destroy callback before this syscall;
 * no message may subsequently target an invalidated handle.
 */
kstd::error_code handle_DestroyWindow_V1(__KRN_USER_DESTROYWINDOW_V1 && args);
/**
 * Hide preserves state, Show preserves state, Restore restores saved geometry.
 * Minimize creates an icon plus label for children in the parent client
 * (roots retain a desktop caption). Click selects a child icon, double-click
 * or Enter restores it; Ctrl+Tab/F6 cycles including minimized child icons.
 * Maximize fills the parent client or desktop. Parent hide/minimize suppresses
 * descendant visibility.
 */
kstd::error_code handle_ShowWindow_V1(__KRN_USER_SHOWWINDOW_V1 && args);
kstd::error_code handle_MoveWindow_V1(__KRN_USER_MOVEWINDOW_V1 && args);
/** Window rectangle in screen coordinates. */
kstd::error_code handle_GetWindowRect_V1(__KRN_USER_GETWINDOWRECT_V1 && args);
/** Adds rect (client coordinates) to the invalid area; whole != 0 invalidates the client. */
kstd::error_code handle_InvalidateRect_V1(__KRN_USER_INVALIDATERECT_V1 && args);
/**
 * Returns a DC clipped to the visible invalid client, origin at the client.
 * Only one paint DC per window; EndPaint must match its hwnd/DC pair.
 */
kstd::error_code handle_BeginPaint_V1(__KRN_USER_BEGINPAINT_V1 && args);
kstd::error_code handle_EndPaint_V1(__KRN_USER_ENDPAINT_V1 && args);
/** Blocks until a message arrives; msg.message == Quit ends a message loop. */
kstd::error_code handle_GetMessage_V1(__KRN_USER_GETMESSAGE_V1 && args);
/** Non-blocking; returns TIMEOUT when the queue is empty. remove == 0 leaves it queued. */
kstd::error_code handle_PeekMessage_V1(__KRN_USER_PEEKMESSAGE_V1 && args);
/** Queues a message; returns OUT_OF_MEMORY when the target ring is full. */
kstd::error_code handle_PostMessage_V1(__KRN_USER_POSTMESSAGE_V1 && args);
/** One of WindowLongIndex. */
kstd::error_code handle_GetWindowLongPtr_V1(__KRN_USER_GETWINDOWLONGPTR_V1 && args);
kstd::error_code handle_SetWindowLongPtr_V1(__KRN_USER_SETWINDOWLONGPTR_V1 && args);
kstd::error_code handle_SetCapture_V1(__KRN_USER_SETCAPTURE_V1 && args);
kstd::error_code handle_ReleaseCapture_V1(__KRN_USER_RELEASECAPTURE_V1 && args);
/** Client bounds {0,0,width,height}; empty while minimized. */
kstd::error_code handle_GetClientRect_V1(__KRN_USER_GETCLIENTRECT_V1 && args);
/**
 * Focus a visible same-process window and activate its root. A minimized
 * child icon may receive focus for Enter/Alt+Space; minimized roots cannot.
 * hwnd == 0 clears this process's focus without changing activation.
 * Key lParam: bits 0..7 scan code, 8..10 key_flags, 16..24 modifier_flags.
 * Char lParam carries the same metadata. Mouse wParam low bits: left=1,right=2;
 * modifier_flags occupy bits 16..24. Mouse lParam packs signed client x/y.
 */
kstd::error_code handle_SetFocus_V1(__KRN_USER_SETFOCUS_V1 && args);
/** Set a same-process window title (at most 63 bytes plus terminator). */
kstd::error_code handle_SetWindowText_V1(__KRN_USER_SETWINDOWTEXT_V1 && args);
/**
 * Reports usable FPU support, not merely the presence of an x87 unit.
 * Native is only valid when the kernel initializes and preserves each thread's
 * FPU state; Emulated is only valid when an instruction emulator is active.
 */
kstd::error_code handle_QueryFpu_V1(__KRN_ABI_QUERYFPU_V1 && args);
/** Reports the version of the network subsystem; UNSUPPORTED when it is not loaded. */
// Provided by module 'network': it must export (UNDOS_MODULE_EXPORT)
//   kstd::error_code handle_QueryNetworkVersion_V1(__KRN_NET_QUERYNETWORKVERSION_V1 &&args);

kstd::error_code handle_Print_V1(__KRN_DEBUG_PRINT_V1 && args);
/**
 * Creates a new process from the executable at `path`. Waits only for loader
 * mapping/dependency/relocation completion, not constructors or child exit.
 * There is no timeout: the bounded loader must signal readiness or exit on
 * failure, so failure never leaves a running child without an owned handle.
 * On failure no process handle is returned.
 * Successful handles may be closed immediately without terminating the child.
 */
kstd::error_code handle_CreateProcess_V1(__KRN_PROC_CREATEPROCESS_V1 && args);
/**
 * Creates a process with explicit standard streams. Same path, argument,
 * flags, readiness and handle ownership contract as CreateProcessV1.
 * Only these streams are inherited; unrelated handles are not inherited.
 * Handles are caller-local open I/O objects, stdin requires read access,
 * stdout/stderr require write access. Child handles are independent copies.
 */
kstd::error_code handle_CreateProcess_V2(__KRN_PROC_CREATEPROCESS_V2 && args);
/** Retrieves information about a running or exited process. */
kstd::error_code handle_GetInfo_V1(__KRN_PROC_GETINFO_V1 && args);
/** Terminates the current process with the given return code. */
kstd::error_code handle_Exit_V1(__KRN_PROC_EXIT_V1 && args);
/** Blocks until a waitable handle (event or process) is signalled. Returns TIMEOUT on expiry. */
kstd::error_code handle_WaitForObject_V1(__KRN_PROC_WAITFOROBJECT_V1 && args);
/** Blocks until a process exits and returns its exit code. Returns TIMEOUT on expiry. */
kstd::error_code handle_WaitProcess_V1(__KRN_PROC_WAITPROCESS_V1 && args);
/** Creates an event object. */
kstd::error_code handle_CreateEvent_V1(__KRN_PROC_CREATEEVENT_V1 && args);
/** Signals an event. */
kstd::error_code handle_SetEvent_V1(__KRN_PROC_SETEVENT_V1 && args);
/**
 * Allocates zeroed pages, rounding size up to 4096. Read is required; unknown
 * flags and no-access/write-only mappings reject. i386 does not enforce NX.
 */
kstd::error_code handle_AllocatePages_V1(__KRN_MEM_ALLOCATEPAGES_V1 && args);
/**
 * Frees a whole eager anonymous region; base must be page aligned and size,
 * rounded up to 4096, must equal the region size. No partial frees or aliases.
 */
kstd::error_code handle_FreePages_V1(__KRN_MEM_FREEPAGES_V1 && args);
/** Reserved: currently rejects with INVALID_ARGUMENT (no partial VAD updates). */
kstd::error_code handle_Protect_V1(__KRN_MEM_PROTECT_V1 && args);
/**
 * Maps zeroed anonymous user pages at a page-aligned address selected by the
 * caller. Read is required; unknown flags and no-access/write-only mappings
 * reject. i386 does not enforce NX.
 */
kstd::error_code handle_MapFixedPages_V1(__KRN_MEM_MAPFIXEDPAGES_V1 && args);
kstd::error_code handle_Open_V1(__KRN_IO_OPEN_V1 && args);
kstd::error_code handle_Close_V1(__KRN_IO_CLOSE_V1 && args);
/** Reads data from an open object handle into `data`. */
kstd::error_code handle_Read_V1(__KRN_IO_READ_V1 && args);
/** Writes data from `data` to an open object handle. */
kstd::error_code handle_Write_V1(__KRN_IO_WRITE_V1 && args);
/** Writes bytes and reports short writes. V1 remains available unchanged. */
kstd::error_code handle_Write_V2(__KRN_IO_WRITE_V2 && args);
/** Sends a control code to the device behind an open handle. */
kstd::error_code handle_Control_V1(__KRN_IO_CONTROL_V1 && args);
/**
 * Returns the opaque handle assigned to a standard stream, or
 * INVALID_ARGUMENT if that stream is not available to the calling process.
 */
kstd::error_code handle_GetStdHandle_V1(__KRN_IO_GETSTDHANDLE_V1 && args);
/** Creates a paired virtual TTY returning source and sink handles. */
kstd::error_code handle_Create_V1(__KRN_TTY_CREATE_V1 && args);
/** Sets TTY configuration flags and window dimensions. */
kstd::error_code handle_SetConfig_V1(__KRN_TTY_SETCONFIG_V1 && args);
/** Retrieves TTY configuration flags and window dimensions. */
kstd::error_code handle_GetConfig_V1(__KRN_TTY_GETCONFIG_V1 && args);

