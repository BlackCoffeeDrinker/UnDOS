// Generated from UnDOS IDL

// See (Tools/create-syscalls.py)
#pragma once

#include <stdint.h>
#include <undos/syscalls/__aliases.h>
#include <kernel/memory/borrowed_ptr.hpp>
#include <string_view.hpp>
#include <undos/syscalls/__enum_PixelFormat.h>
#include <undos/syscalls/__enum_RasterOp.h>
#include <undos/syscalls/__enum_WindowMessage.h>
#include <undos/syscalls/__enum_ShowCommand.h>
#include <undos/syscalls/__enum_HitTest.h>
#include <undos/syscalls/__enum_WindowLongIndex.h>
#include <undos/syscalls/__enum_FpuCapability.h>
#include <undos/syscalls/__enum_SyscallReturnCode.h>
#include <undos/syscalls/__enum_ProcessFlags.h>
#include <undos/syscalls/__enum_WaitTimeout.h>
#include <undos/syscalls/__enum_MemoryProtectFlags.h>
#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__enum_StdHandleSelector.h>

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
#include <undos/syscalls/__struct_DisplayMode.h>
#include <undos/syscalls/__struct_DisplayState.h>
#include <undos/syscalls/__struct_TextMetrics.h>
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
    HANDLER dc;
};

struct __KRN_GFX_SELECTOBJECT_V1 {
    HANDLER dc;
    HANDLER object;
};

struct __KRN_GFX_CREATEPEN_V1 {
    enum PenStyle style;
    uint32_t width;
    COLORREF color;
};

struct __KRN_GFX_CREATEBRUSH_V1 {
    enum BrushPattern pattern;
    COLORREF color;
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
    HANDLER object;
};

struct __KRN_GFX_MOVETO_V1 {
    HANDLER dc;
    int32_t x;
    int32_t y;
};

struct __KRN_GFX_LINETO_V1 {
    HANDLER dc;
    int32_t x;
    int32_t y;
};

struct __KRN_GFX_RECTANGLE_V1 {
    HANDLER dc;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct __KRN_GFX_ELLIPSE_V1 {
    HANDLER dc;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct __KRN_GFX_TEXTOUT_V1 {
    HANDLER dc;
    int32_t x;
    int32_t y;
    kstd::string text {};
};

struct __KRN_GFX_BITBLT_V1 {
    HANDLER dstDC;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    HANDLER srcDC;
    int32_t srcX;
    int32_t srcY;
    enum RasterOp rop;
};

struct __KRN_GFX_STRETCHBLT_V1 {
    HANDLER dstDC;
    int32_t x;
    int32_t y;
    uint32_t dstWidth;
    uint32_t dstHeight;
    HANDLER srcDC;
    int32_t srcX;
    int32_t srcY;
    uint32_t srcWidth;
    uint32_t srcHeight;
    enum RasterOp rop;
};

struct __KRN_GFX_ALPHABLEND_V1 {
    HANDLER dstDC;
    HANDLER srcDC;
    struct Rect dstRect;
    struct Rect srcRect;
    uint8_t alpha;
};

struct __KRN_GFX_CREATEREGION_V1 {
    kernel::borrowed_ptr<struct Rect> rects = nullptr;
};

struct __KRN_GFX_SELECTCLIPREGION_V1 {
    HANDLER dc;
    HANDLER region;
};

struct __KRN_GFX_COMBINEREGION_V1 {
    HANDLER dest;
    HANDLER src1;
    HANDLER src2;
    enum RegionCombineMode mode;
};

struct __KRN_GFX_SETVIEWPORTORG_V1 {
    HANDLER dc;
    int32_t x;
    int32_t y;
};

struct __KRN_GFX_SETWINDOWORG_V1 {
    HANDLER dc;
    int32_t x;
    int32_t y;
};

struct __KRN_GFX_GETTEXTMETRICS_V1 {
    HANDLER dc;
    struct TextMetrics metrics;
};

struct __KRN_GFX_GETBITMAPBITS_V1 {
    HANDLER bitmap;
    kernel::borrowed_ptr<uint8_t> buffer = nullptr;
    size_t buffer_len = 0;
};

struct __KRN_GFX_SETBITMAPBITS_V1 {
    HANDLER bitmap;
    kernel::borrowed_ptr<uint8_t> buffer = nullptr;
    size_t buffer_len = 0;
};

struct __KRN_GFX_GETDEVICECAPS_V1 {
    HANDLER dc;
    struct DeviceCaps caps;
};

struct __KRN_GFX_SETDRAWCOLORS_V1 {
    uint32_t dc;
    COLORREF foreground;
    COLORREF background;
    uint32_t opaque;
};

struct __KRN_GFX_QUERYFONTENGINE_V1 {
    uint32_t version;
    uint32_t capabilities;
};

struct __KRN_GFX_BEGINFONTUPLOAD_V1 {
    uint32_t totalBytes;
    uint32_t upload;
};

struct __KRN_GFX_APPENDFONTUPLOAD_V1 {
    uint32_t upload;
    uint32_t offset;
    kernel::borrowed_ptr<uint8_t> data = nullptr;
    size_t data_len = 0;
};

struct __KRN_GFX_COMMITFONTUPLOAD_V1 {
    uint32_t upload;
    uint32_t face;
    uint32_t validationError;
};

struct __KRN_GFX_CLOSEFONTRESOURCE_V1 {
    uint32_t handle;
};

/**
 * Enumerates monitor-approved primary-display modes without activating output.
 * Ordinals start at zero. OKAY with available=0 and a zero mode marks the end.
 * UNSUPPORTED means no usable primary display.
 */
struct __KRN_GFX_ENUMDISPLAYMODES_V1 {
    uint32_t index;
    struct DisplayMode mode;
    uint32_t available;
};

/**
 * Queries the primary display, including its inactive pre-graphics state.
 * Returns UNSUPPORTED when no primary display exists.
 */
struct __KRN_GFX_GETDISPLAYMODE_V1 {
    struct DisplayState state;
};

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
struct __KRN_GFX_SETDISPLAYMODE_V1 {
    uint32_t modeId;
};

/** Registers a process-scoped window class; wndProc is opaque and called by userland. */
struct __KRN_USER_REGISTERCLASS_V1 {
    kstd::string name {};
    uint32_t wndProc;
    COLORREF background;
};

/**
 * Creates a window owned by the calling thread. Child requires a same-process
 * parent; x/y are relative to its client origin. Otherwise parent must be zero.
 * Coordinates and dimensions must fit the signed 16-bit screen range.
 */
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

/**
 * Immediately destroys the window and descendants and removes their queued
 * messages. Userland dispatches any Destroy callback before this syscall;
 * no message may subsequently target an invalidated handle.
 */
struct __KRN_USER_DESTROYWINDOW_V1 {
    uint32_t hwnd;
};

/**
 * Hide preserves state, Show preserves state, Restore restores saved geometry.
 * Minimize creates an icon plus label for children in the parent client
 * (roots retain a desktop caption). Click selects a child icon, double-click
 * or Enter restores it; Ctrl+Tab/F6 cycles including minimized child icons.
 * Maximize fills the parent client or desktop. Parent hide/minimize suppresses
 * descendant visibility.
 */
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

/** Adds rect (client coordinates) to the invalid area; whole != 0 invalidates the client. */
struct __KRN_USER_INVALIDATERECT_V1 {
    uint32_t hwnd;
    struct Rect rect;
    uint32_t whole;
};

/**
 * Returns a DC clipped to the visible invalid client, origin at the client.
 * Only one paint DC per window; EndPaint must match its hwnd/DC pair.
 */
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

/** Client bounds {0,0,width,height}; empty while minimized. */
struct __KRN_USER_GETCLIENTRECT_V1 {
    uint32_t hwnd;
    struct Rect rect;
};

/**
 * Focus a visible same-process window and activate its root. A minimized
 * child icon may receive focus for Enter/Alt+Space; minimized roots cannot.
 * hwnd == 0 clears this process's focus without changing activation.
 * Key lParam: bits 0..7 scan code, 8..10 key_flags, 16..24 modifier_flags.
 * Char lParam carries the same metadata. Mouse wParam low bits: left=1,right=2;
 * modifier_flags occupy bits 16..24. Mouse lParam packs signed client x/y.
 */
struct __KRN_USER_SETFOCUS_V1 {
    uint32_t hwnd;
};

/** Set a same-process window title (at most 63 bytes plus terminator). */
struct __KRN_USER_SETWINDOWTEXT_V1 {
    uint32_t hwnd;
    kstd::string text {};
};

/**
 * Reports usable FPU support, not merely the presence of an x87 unit.
 * Native is only valid when the kernel initializes and preserves each thread's
 * FPU state; Emulated is only valid when an instruction emulator is active.
 */
struct __KRN_ABI_QUERYFPU_V1 {
    enum FpuCapability capability;
};

/** Reports the version of the network subsystem; UNSUPPORTED when it is not loaded. */
struct __KRN_NET_QUERYNETWORKVERSION_V1 {
    /** Interface version implemented by the network subsystem. */
    uint32_t version;
};

struct __KRN_DEBUG_PRINT_V1 {
    kstd::string message {};
};

/**
 * Creates a new process from the executable at `path`. Waits only for loader
 * mapping/dependency/relocation completion, not constructors or child exit.
 * There is no timeout: the bounded loader must signal readiness or exit on
 * failure, so failure never leaves a running child without an owned handle.
 * On failure no process handle is returned.
 * Successful handles may be closed immediately without terminating the child.
 */
struct __KRN_PROC_CREATEPROCESS_V1 {
    /** Nonempty executable path, at most 255 bytes; becomes argv[0] verbatim. */
    kstd::string path {};
    /**
     * At most 1023 bytes, split on ASCII whitespace outside double quotes.
     * Quotes are removed, adjacent fragments concatenate, "" is an empty arg.
     * Backslashes are literal; unmatched quotes/NULs reject. Max argc is 32.
     */
    kstd::string argument {};
    /** Only ProcessFlags_None (zero) is supported; all other flags reject. */
    uint32_t flags;
    /** Handle to the new process (waitable with WaitProcess). */
    HANDLE handle;
};

/**
 * Creates a process with explicit standard streams. Same path, argument,
 * flags, readiness and handle ownership contract as CreateProcessV1.
 * Only these streams are inherited; unrelated handles are not inherited.
 * Handles are caller-local open I/O objects, stdin requires read access,
 * stdout/stderr require write access. Child handles are independent copies.
 */
struct __KRN_PROC_CREATEPROCESS_V2 {
    kstd::string path {};
    kstd::string argument {};
    uint32_t flags;
    HANDLE stdinHandle;
    HANDLE stdoutHandle;
    HANDLE stderrHandle;
    HANDLE handle;
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
    HANDLE handle;
    /** Timeout in milliseconds; 0 polls, WaitTimeout_Infinite never expires. */
    uint32_t timeoutMs;
};

/** Blocks until a process exits and returns its exit code. Returns TIMEOUT on expiry. */
struct __KRN_PROC_WAITPROCESS_V1 {
    /** Process handle from CreateProcess. */
    HANDLE handle;
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
    HANDLE handle;
};

/** Signals an event. */
struct __KRN_PROC_SETEVENT_V1 {
    /** Event handle. */
    HANDLE handle;
};

/**
 * Allocates zeroed pages, rounding size up to 4096. Read is required; unknown
 * flags and no-access/write-only mappings reject. i386 does not enforce NX.
 */
struct __KRN_MEM_ALLOCATEPAGES_V1 {
    /** Number of bytes to allocate (rounded up to a page boundary). */
    uint32_t size;
    /** Protection to apply to the newly allocated pages. */
    enum MemoryProtectFlags flags;
    /** Base address of the allocated region. */
    uint32_t address;
};

/**
 * Frees a whole eager anonymous region; base must be page aligned and size,
 * rounded up to 4096, must equal the region size. No partial frees or aliases.
 */
struct __KRN_MEM_FREEPAGES_V1 {
    /** Base address of the region to free. */
    uint32_t address;
    /** Size in bytes of the region to free. */
    uint32_t size;
};

/** Reserved: currently rejects with INVALID_ARGUMENT (no partial VAD updates). */
struct __KRN_MEM_PROTECT_V1 {
    /** Base address of the region to reprotect. */
    uint32_t address;
    /** Size in bytes of the region to reprotect. */
    uint32_t size;
    /** New protection to apply. */
    enum MemoryProtectFlags flags;
};

/**
 * Maps zeroed anonymous user pages at a page-aligned address selected by the
 * caller. Read is required; unknown flags and no-access/write-only mappings
 * reject. i386 does not enforce NX.
 */
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
    HANDLE handle;
};

struct __KRN_IO_CLOSE_V1 {
    /** Open handle to close. */
    HANDLE handle;
};

/** Reads data from an open object handle into `data`. */
struct __KRN_IO_READ_V1 {
    /** Open handle to read from. */
    HANDLE handle;
    /** Buffer that receives the data read. */
    kernel::borrowed_ptr<uint8_t> data = nullptr;
    size_t data_len = 0;
};

/** Writes data from `data` to an open object handle. */
struct __KRN_IO_WRITE_V1 {
    /** Open handle to write to. */
    HANDLE handle;
    /** Buffer containing the data to write. */
    kernel::borrowed_ptr<uint8_t> data = nullptr;
    size_t data_len = 0;
};

/** Writes bytes and reports short writes. V1 remains available unchanged. */
struct __KRN_IO_WRITE_V2 {
    /** Open handle with write access. */
    HANDLE handle;
    /** Bytes to write, at most 4096 per call. Use null with length zero for an empty write. */
    kernel::borrowed_ptr<uint8_t> data = nullptr;
    size_t data_len = 0;
    /**
     * Actual bytes accepted, at most dataLength. Zero on handler failure;
     * undefined if dispatcher validation rejects the call before the handler.
     */
    uint32_t transferred;
};

/** Sends a control code to the device behind an open handle. */
struct __KRN_IO_CONTROL_V1 {
    /** Open handle to the device. */
    HANDLE handle;
    /** One of IoControlCode. */
    uint32_t code;
    /** Code-specific input payload. */
    kernel::borrowed_ptr<uint8_t> input = nullptr;
    size_t input_len = 0;
    /** Code-specific output payload; receives the number of bytes written. */
    kernel::borrowed_ptr<uint8_t> output = nullptr;
    size_t output_len = 0;
};

/**
 * Returns the opaque handle assigned to a standard stream, or
 * INVALID_ARGUMENT if that stream is not available to the calling process.
 */
struct __KRN_IO_GETSTDHANDLE_V1 {
    /** One of StdHandleSelector. */
    enum StdHandleSelector selector;
    /** Process-local opaque handle for the selected stream. */
    HANDLE handle;
};

/** Creates a paired virtual TTY returning source and sink handles. */
struct __KRN_TTY_CREATE_V1 {
    HANDLE sourceHandle;
    HANDLE sinkHandle;
};

/** Sets TTY configuration flags and window dimensions. */
struct __KRN_TTY_SETCONFIG_V1 {
    HANDLE handle;
    enum TtyModeFlags flags;
    uint16_t rows;
    uint16_t cols;
};

/** Retrieves TTY configuration flags and window dimensions. */
struct __KRN_TTY_GETCONFIG_V1 {
    HANDLE handle;
    enum TtyModeFlags flags;
    uint16_t rows;
    uint16_t cols;
};

