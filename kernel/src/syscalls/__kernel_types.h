// Generated from UnDOS IDL

// See (tools/create-syscalls.py)
#pragma once

#include <stdint.h>
#include <kernel/memory/borrowed_ptr.hpp>
#include <string_view.hpp>
#include <undos/syscalls/__enum_PixelFormat.h>
#include <undos/syscalls/__enum_RasterOp.h>
#include <undos/syscalls/__enum_WindowMessage.h>
#include <undos/syscalls/__enum_ShowCommand.h>
#include <undos/syscalls/__enum_HitTest.h>
#include <undos/syscalls/__enum_WindowLongIndex.h>
#include <undos/syscalls/__enum_SyscallReturnCode.h>
#include <undos/syscalls/__enum_ProcessFlags.h>
#include <undos/syscalls/__enum_WaitTimeout.h>
#include <undos/syscalls/__enum_MemoryProtectFlags.h>
#include <undos/syscalls/__enum_IoControlCode.h>

#include <undos/syscalls/__flags_DCInitFlags.h>
#include <undos/syscalls/__flags_PenStyle.h>
#include <undos/syscalls/__flags_BrushPattern.h>
#include <undos/syscalls/__flags_RegionCombineMode.h>
#include <undos/syscalls/__flags_Device3DFlags.h>
#include <undos/syscalls/__flags_WindowStyle.h>
#include <undos/syscalls/__flags_EventFlags.h>
#include <undos/syscalls/__flags_FileOpenMode.h>
#include <undos/syscalls/__flags_TtyModeFlags.h>

#include <undos/syscalls/__struct_Point.h>
#include <undos/syscalls/__struct_Rect.h>
#include <undos/syscalls/__struct_DeviceCaps.h>
#include <undos/syscalls/__struct_Msg.h>
#include <undos/syscalls/__struct_PaintInfo.h>
#include <undos/syscalls/__struct_IoctlDisplayMode.h>
#include <undos/syscalls/__struct_IoctlMouseConfig.h>
#include <undos/syscalls/__struct_IoctlGamepadState.h>
#include <undos/syscalls/__struct_IoctlGamepadCaps.h>

struct __KRN_GFX_CREATEDC_V1 {
    kstd::string driverName {};
    kstd::string deviceName {};
    enum DCInitFlags initFlags;
    uint32_t handle;
};

struct __KRN_GFX_DELETEDC_V1 {
    uint32_t dc;
};

struct __KRN_GFX_SELECTOBJECT_V1 {
    uint32_t dc;
    uint32_t object;
};

struct __KRN_GFX_CREATEPEN_V1 {
    enum PenStyle style;
    uint32_t width;
    uint32_t color;
};

struct __KRN_GFX_CREATEBRUSH_V1 {
    enum BrushPattern pattern;
    uint32_t color;
};

struct __KRN_GFX_CREATEFONT_V1 {
    int32_t height;
    uint32_t weight;
    uint32_t charset;
    kstd::string name {};
};

struct __KRN_GFX_CREATEBITMAP_V1 {
    uint32_t width;
    uint32_t height;
    enum PixelFormat format;
    kernel::borrowed_ptr<uint8_t> pixels = nullptr;
    size_t pixels_len = 0;
    uint32_t handle;
};

struct __KRN_GFX_DELETEOBJECT_V1 {
    uint32_t object;
};

struct __KRN_GFX_MOVETO_V1 {
    uint32_t dc;
    int32_t x;
    int32_t y;
};

struct __KRN_GFX_LINETO_V1 {
    uint32_t dc;
    int32_t x;
    int32_t y;
};

struct __KRN_GFX_RECTANGLE_V1 {
    uint32_t dc;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct __KRN_GFX_ELLIPSE_V1 {
    uint32_t dc;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct __KRN_GFX_TEXTOUT_V1 {
    uint32_t dc;
    int32_t x;
    int32_t y;
    kstd::string text {};
};

struct __KRN_GFX_BITBLT_V1 {
    uint32_t dstDC;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t srcDC;
    int32_t srcX;
    int32_t srcY;
    enum RasterOp rop;
};

struct __KRN_GFX_STRETCHBLT_V1 {
    uint32_t dstDC;
    int32_t x;
    int32_t y;
    uint32_t dstWidth;
    uint32_t dstHeight;
    uint32_t srcDC;
    int32_t srcX;
    int32_t srcY;
    uint32_t srcWidth;
    uint32_t srcHeight;
    enum RasterOp rop;
};

struct __KRN_GFX_ALPHABLEND_V1 {
    uint32_t dstDC;
    uint32_t srcDC;
    struct Rect dstRect;
    struct Rect srcRect;
    uint8_t alpha;
};

struct __KRN_GFX_CREATEREGION_V1 {
    kernel::borrowed_ptr<struct Rect> rects = nullptr;
};

struct __KRN_GFX_SELECTCLIPREGION_V1 {
    uint32_t dc;
    uint32_t region;
};

struct __KRN_GFX_COMBINEREGION_V1 {
    uint32_t dest;
    uint32_t src1;
    uint32_t src2;
    enum RegionCombineMode mode;
};

struct __KRN_GFX_SETVIEWPORTORG_V1 {
    uint32_t dc;
    int32_t x;
    int32_t y;
};

struct __KRN_GFX_SETWINDOWORG_V1 {
    uint32_t dc;
    int32_t x;
    int32_t y;
};

struct __KRN_GFX_GETTEXTMETRICS_V1 {
    uint32_t dc;
    struct DeviceCaps metrics;
};

struct __KRN_GFX_GETBITMAPBITS_V1 {
    uint32_t bitmap;
    kernel::borrowed_ptr<uint8_t> buffer = nullptr;
    size_t buffer_len = 0;
};

struct __KRN_GFX_SETBITMAPBITS_V1 {
    uint32_t bitmap;
    kernel::borrowed_ptr<uint8_t> buffer = nullptr;
    size_t buffer_len = 0;
};

struct __KRN_GFX_GETDEVICECAPS_V1 {
    uint32_t dc;
    struct DeviceCaps caps;
};

/** Registers a window class; wndProc is stored opaquely and called by userland. */
struct __KRN_USER_REGISTERCLASS_V1 {
    kstd::string name {};
    uint32_t wndProc;
    uint32_t background;
};

/** Creates a top-level window owned by the calling thread. */
struct __KRN_USER_CREATEWINDOW_V1 {
    kstd::string className {};
    kstd::string title {};
    enum WindowStyle style;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t parent;
    uint32_t userData;
    uint32_t hwnd;
};

struct __KRN_USER_DESTROYWINDOW_V1 {
    uint32_t hwnd;
};

/** One of ShowCommand. */
struct __KRN_USER_SHOWWINDOW_V1 {
    uint32_t hwnd;
    uint32_t command;
};

struct __KRN_USER_MOVEWINDOW_V1 {
    uint32_t hwnd;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

/** Window rectangle in screen coordinates. */
struct __KRN_USER_GETWINDOWRECT_V1 {
    uint32_t hwnd;
    struct Rect rect;
};

/** Adds rect (window coordinates) to the invalid area; whole != 0 invalidates everything. */
struct __KRN_USER_INVALIDATERECT_V1 {
    uint32_t hwnd;
    struct Rect rect;
    uint32_t whole;
};

/** Returns a DC clipped to the visible part of the invalid area, origin at the window. */
struct __KRN_USER_BEGINPAINT_V1 {
    uint32_t hwnd;
    uint32_t dc;
    struct PaintInfo paint;
};

struct __KRN_USER_ENDPAINT_V1 {
    uint32_t hwnd;
    uint32_t dc;
};

/** Blocks until a message arrives; msg.message == Quit ends a message loop. */
struct __KRN_USER_GETMESSAGE_V1 {
    struct Msg msg;
};

/** Non-blocking; returns TIMEOUT when the queue is empty. remove == 0 leaves it queued. */
struct __KRN_USER_PEEKMESSAGE_V1 {
    struct Msg msg;
    uint32_t remove;
};

/** Queues a message; returns OUT_OF_MEMORY when the target ring is full. */
struct __KRN_USER_POSTMESSAGE_V1 {
    uint32_t hwnd;
    uint32_t message;
    uint32_t wParam;
    uint32_t lParam;
};

/** One of WindowLongIndex. */
struct __KRN_USER_GETWINDOWLONGPTR_V1 {
    uint32_t hwnd;
    uint32_t index;
    uint32_t value;
};

struct __KRN_USER_SETWINDOWLONGPTR_V1 {
    uint32_t hwnd;
    uint32_t index;
    uint32_t value;
    uint32_t previous;
};

struct __KRN_USER_SETCAPTURE_V1 {
    uint32_t hwnd;
};

struct __KRN_USER_RELEASECAPTURE_V1 {
    uint32_t unused;
};

struct __KRN_DEBUG_PRINT_V1 {
    kstd::string message {};
};

/** Creates a new process from the executable at `path`. */
struct __KRN_PROC_CREATEPROCESS_V1 {
    /** Path to the executable to load. */
    kstd::string path {};
    /** Command-line argument string passed to the new process. */
    kstd::string argument {};
    /** Combination of ProcessFlags controlling the new process. */
    uint32_t flags;
    /** Handle to the new process (waitable with WaitProcess). */
    uint32_t handle;
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

/** Blocks until a waitable handle (event or process) is signalled. Returns TIMEOUT on expiry. */
struct __KRN_PROC_WAITFOROBJECT_V1 {
    /** Handle to wait on. */
    uint32_t handle;
    /** Timeout in milliseconds; 0 polls, WaitTimeout_Infinite never expires. */
    uint32_t timeoutMs;
};

/** Blocks until a process exits and returns its exit code. Returns TIMEOUT on expiry. */
struct __KRN_PROC_WAITPROCESS_V1 {
    /** Process handle from CreateProcess. */
    uint32_t handle;
    /** Timeout in milliseconds; 0 polls, WaitTimeout_Infinite never expires. */
    uint32_t timeoutMs;
    /** Exit code of the process. */
    uint32_t exitCode;
};

/** Creates an event object. */
struct __KRN_PROC_CREATEEVENT_V1 {
    /** Combination of EventFlags. */
    enum EventFlags flags;
    /** Handle to the new event. */
    uint32_t handle;
};

/** Signals an event. */
struct __KRN_PROC_SETEVENT_V1 {
    /** Event handle. */
    uint32_t handle;
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

/** Maps anonymous user pages at a page-aligned address selected by the caller. */
struct __KRN_MEM_MAPFIXEDPAGES_V1 {
    uint32_t address;
    uint32_t size;
    enum MemoryProtectFlags flags;
};

struct __KRN_IO_OPEN_V1 {
    /** Path to the object to open. */
    kstd::string path {};
    /** Open mode (read, write, append). */
    enum FileOpenMode mode;
    /** Handle to receive the opened object. */
    uint32_t handle;
};

struct __KRN_IO_CLOSE_V1 {
    /** Open handle to close. */
    uint32_t handle;
};

/** Reads data from an open object handle into `data`. */
struct __KRN_IO_READ_V1 {
    /** Open handle to read from. */
    uint32_t handle;
    /** Buffer that receives the data read. */
    kernel::borrowed_ptr<uint8_t> data = nullptr;
    size_t data_len = 0;
};

/** Writes data from `data` to an open object handle. */
struct __KRN_IO_WRITE_V1 {
    /** Open handle to write to. */
    uint32_t handle;
    /** Buffer containing the data to write. */
    kernel::borrowed_ptr<uint8_t> data = nullptr;
    size_t data_len = 0;
};

/** Sends a control code to the device behind an open handle. */
struct __KRN_IO_CONTROL_V1 {
    /** Open handle to the device. */
    uint32_t handle;
    /** One of IoControlCode. */
    uint32_t code;
    /** Code-specific input payload. */
    kernel::borrowed_ptr<uint8_t> input = nullptr;
    size_t input_len = 0;
    /** Code-specific output payload; receives the number of bytes written. */
    kernel::borrowed_ptr<uint8_t> output = nullptr;
    size_t output_len = 0;
};

/** Creates a paired virtual TTY returning source and sink handles. */
struct __KRN_TTY_CREATE_V1 {
    uint32_t sourceHandle;
    uint32_t sinkHandle;
};

/** Sets TTY configuration flags and window dimensions. */
struct __KRN_TTY_SETCONFIG_V1 {
    uint32_t handle;
    enum TtyModeFlags flags;
    uint16_t rows;
    uint16_t cols;
};

/** Retrieves TTY configuration flags and window dimensions. */
struct __KRN_TTY_GETCONFIG_V1 {
    uint32_t handle;
    enum TtyModeFlags flags;
    uint16_t rows;
    uint16_t cols;
};

