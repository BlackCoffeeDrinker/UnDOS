#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

#include <undos/syscalls.h>

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateDCV1(const char* driverName, const char* deviceName, enum DCInitFlags initFlags, uint32_t* handle)
{
    uint32_t ret = 0;
    struct __SYS_GFX_CREATEDC_V1 __wire;
    __wire.driverName = driverName;
    __wire.driverNameLength = 0;
    if (driverName) while (driverName[__wire.driverNameLength]) __wire.driverNameLength++;
    __wire.deviceName = deviceName;
    __wire.deviceNameLength = 0;
    if (deviceName) while (deviceName[__wire.deviceNameLength]) __wire.deviceNameLength++;
    __wire.initFlags = initFlags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(300), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *handle = __wire.handle;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_DeleteDCV1(HANDLER dc)
{
    uint32_t ret = 0;
    struct __SYS_GFX_DELETEDC_V1 __wire;
    __wire.dc = dc;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(301), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SelectObjectV1(HANDLER dc, HANDLER object)
{
    uint32_t ret = 0;
    struct __SYS_GFX_SELECTOBJECT_V1 __wire;
    __wire.dc = dc;
    __wire.object = object;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(302), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreatePenV1(enum PenStyle style, uint32_t width, COLORREF color)
{
    uint32_t ret = 0;
    struct __SYS_GFX_CREATEPEN_V1 __wire;
    __wire.style = style;
    __wire.width = width;
    __wire.color = color;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(303), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateBrushV1(enum BrushPattern pattern, COLORREF color)
{
    uint32_t ret = 0;
    struct __SYS_GFX_CREATEBRUSH_V1 __wire;
    __wire.pattern = pattern;
    __wire.color = color;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(304), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateFontV1(int32_t height, uint32_t weight, uint32_t charset, const char* name)
{
    uint32_t ret = 0;
    struct __SYS_GFX_CREATEFONT_V1 __wire;
    __wire.height = height;
    __wire.weight = weight;
    __wire.charset = charset;
    __wire.name = name;
    __wire.nameLength = 0;
    if (name) while (name[__wire.nameLength]) __wire.nameLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(305), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateBitmapV1(uint32_t width, uint32_t height, enum PixelFormat format, const uint8_t* pixels, uint32_t pixelsLength, uint32_t* handle)
{
    uint32_t ret = 0;
    struct __SYS_GFX_CREATEBITMAP_V1 __wire;
    __wire.width = width;
    __wire.height = height;
    __wire.format = format;
    __wire.pixels = pixels;
    __wire.pixelsLength = pixelsLength;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(306), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *handle = __wire.handle;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_DeleteObjectV1(HANDLER object)
{
    uint32_t ret = 0;
    struct __SYS_GFX_DELETEOBJECT_V1 __wire;
    __wire.object = object;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(307), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_MoveToV1(HANDLER dc, int32_t x, int32_t y)
{
    uint32_t ret = 0;
    struct __SYS_GFX_MOVETO_V1 __wire;
    __wire.dc = dc;
    __wire.x = x;
    __wire.y = y;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(310), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_LineToV1(HANDLER dc, int32_t x, int32_t y)
{
    uint32_t ret = 0;
    struct __SYS_GFX_LINETO_V1 __wire;
    __wire.dc = dc;
    __wire.x = x;
    __wire.y = y;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(311), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_RectangleV1(HANDLER dc, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    uint32_t ret = 0;
    struct __SYS_GFX_RECTANGLE_V1 __wire;
    __wire.dc = dc;
    __wire.left = left;
    __wire.top = top;
    __wire.right = right;
    __wire.bottom = bottom;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(312), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_EllipseV1(HANDLER dc, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    uint32_t ret = 0;
    struct __SYS_GFX_ELLIPSE_V1 __wire;
    __wire.dc = dc;
    __wire.left = left;
    __wire.top = top;
    __wire.right = right;
    __wire.bottom = bottom;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(313), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_TextOutV1(HANDLER dc, int32_t x, int32_t y, const char* text)
{
    uint32_t ret = 0;
    struct __SYS_GFX_TEXTOUT_V1 __wire;
    __wire.dc = dc;
    __wire.x = x;
    __wire.y = y;
    __wire.text = text;
    __wire.textLength = 0;
    if (text) while (text[__wire.textLength]) __wire.textLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(314), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_BitBltV1(HANDLER dstDC, int32_t x, int32_t y, uint32_t width, uint32_t height, HANDLER srcDC, int32_t srcX, int32_t srcY, enum RasterOp rop)
{
    uint32_t ret = 0;
    struct __SYS_GFX_BITBLT_V1 __wire;
    __wire.dstDC = dstDC;
    __wire.x = x;
    __wire.y = y;
    __wire.width = width;
    __wire.height = height;
    __wire.srcDC = srcDC;
    __wire.srcX = srcX;
    __wire.srcY = srcY;
    __wire.rop = rop;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(320), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_StretchBltV1(HANDLER dstDC, int32_t x, int32_t y, uint32_t dstWidth, uint32_t dstHeight, HANDLER srcDC, int32_t srcX, int32_t srcY, uint32_t srcWidth, uint32_t srcHeight, enum RasterOp rop)
{
    uint32_t ret = 0;
    struct __SYS_GFX_STRETCHBLT_V1 __wire;
    __wire.dstDC = dstDC;
    __wire.x = x;
    __wire.y = y;
    __wire.dstWidth = dstWidth;
    __wire.dstHeight = dstHeight;
    __wire.srcDC = srcDC;
    __wire.srcX = srcX;
    __wire.srcY = srcY;
    __wire.srcWidth = srcWidth;
    __wire.srcHeight = srcHeight;
    __wire.rop = rop;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(321), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_AlphaBlendV1(HANDLER dstDC, HANDLER srcDC, const struct Rect* dstRect, const struct Rect* srcRect, uint8_t alpha)
{
    uint32_t ret = 0;
    struct __SYS_GFX_ALPHABLEND_V1 __wire;
    __wire.dstDC = dstDC;
    __wire.srcDC = srcDC;
    __wire.dstRect = *dstRect;
    __wire.srcRect = *srcRect;
    __wire.alpha = alpha;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(322), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateRegionV1(const struct Rect* rects, uint32_t rectsLength)
{
    uint32_t ret = 0;
    struct __SYS_GFX_CREATEREGION_V1 __wire;
    __wire.rects = rects;
    __wire.rectsLength = rectsLength;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(330), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SelectClipRegionV1(HANDLER dc, HANDLER region)
{
    uint32_t ret = 0;
    struct __SYS_GFX_SELECTCLIPREGION_V1 __wire;
    __wire.dc = dc;
    __wire.region = region;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(331), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CombineRegionV1(HANDLER dest, HANDLER src1, HANDLER src2, enum RegionCombineMode mode)
{
    uint32_t ret = 0;
    struct __SYS_GFX_COMBINEREGION_V1 __wire;
    __wire.dest = dest;
    __wire.src1 = src1;
    __wire.src2 = src2;
    __wire.mode = mode;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(332), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetViewportOrgV1(HANDLER dc, int32_t x, int32_t y)
{
    uint32_t ret = 0;
    struct __SYS_GFX_SETVIEWPORTORG_V1 __wire;
    __wire.dc = dc;
    __wire.x = x;
    __wire.y = y;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(333), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetWindowOrgV1(HANDLER dc, int32_t x, int32_t y)
{
    uint32_t ret = 0;
    struct __SYS_GFX_SETWINDOWORG_V1 __wire;
    __wire.dc = dc;
    __wire.x = x;
    __wire.y = y;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(334), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetTextMetricsV1(HANDLER dc, struct TextMetrics* metrics)
{
    uint32_t ret = 0;
    struct __SYS_GFX_GETTEXTMETRICS_V1 __wire;
    __wire.dc = dc;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(340), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *metrics = __wire.metrics;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetBitmapBitsV1(HANDLER bitmap, uint8_t* buffer, uint32_t* bufferLength)
{
    uint32_t ret = 0;
    struct __SYS_GFX_GETBITMAPBITS_V1 __wire;
    __wire.bitmap = bitmap;
    __wire.buffer = buffer;
    __wire.bufferLength = bufferLength ? *bufferLength : 0;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(341), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    /* buffer was mapped and filled by the kernel directly; report actual length written */
    if (bufferLength) *bufferLength = __wire.bufferLength;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetBitmapBitsV1(HANDLER bitmap, const uint8_t* buffer, uint32_t bufferLength)
{
    uint32_t ret = 0;
    struct __SYS_GFX_SETBITMAPBITS_V1 __wire;
    __wire.bitmap = bitmap;
    __wire.buffer = buffer;
    __wire.bufferLength = bufferLength;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(342), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetDeviceCapsV1(HANDLER dc, struct DeviceCaps* caps)
{
    uint32_t ret = 0;
    struct __SYS_GFX_GETDEVICECAPS_V1 __wire;
    __wire.dc = dc;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(343), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *caps = __wire.caps;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetDrawColorsV1(uint32_t dc, COLORREF foreground, COLORREF background, uint32_t opaque)
{
    uint32_t ret = 0;
    struct __SYS_GFX_SETDRAWCOLORS_V1 __wire;
    __wire.dc = dc;
    __wire.foreground = foreground;
    __wire.background = background;
    __wire.opaque = opaque;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(344), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_QueryFontEngineV1(uint32_t* version, uint32_t* capabilities)
{
    uint32_t ret = 0;
    struct __SYS_GFX_QUERYFONTENGINE_V1 __wire;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(345), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *version = __wire.version;
    *capabilities = __wire.capabilities;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_BeginFontUploadV1(uint32_t totalBytes, uint32_t* upload)
{
    uint32_t ret = 0;
    struct __SYS_GFX_BEGINFONTUPLOAD_V1 __wire;
    __wire.totalBytes = totalBytes;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(346), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *upload = __wire.upload;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_AppendFontUploadV1(uint32_t upload, uint32_t offset, const uint8_t* data, uint32_t dataLength)
{
    uint32_t ret = 0;
    struct __SYS_GFX_APPENDFONTUPLOAD_V1 __wire;
    __wire.upload = upload;
    __wire.offset = offset;
    __wire.data = data;
    __wire.dataLength = dataLength;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(347), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CommitFontUploadV1(uint32_t upload, uint32_t* face, uint32_t* validationError)
{
    uint32_t ret = 0;
    struct __SYS_GFX_COMMITFONTUPLOAD_V1 __wire;
    __wire.upload = upload;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(348), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *face = __wire.face;
    *validationError = __wire.validationError;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CloseFontResourceV1(uint32_t handle)
{
    uint32_t ret = 0;
    struct __SYS_GFX_CLOSEFONTRESOURCE_V1 __wire;
    __wire.handle = handle;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(349), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/**
 * Enumerates monitor-approved primary-display modes without activating output.
 * Ordinals start at zero. OKAY with available=0 and a zero mode marks the end.
 * UNSUPPORTED means no usable primary display.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_EnumDisplayModesV1(uint32_t index, struct DisplayMode* mode, uint32_t* available)
{
    uint32_t ret = 0;
    struct __SYS_GFX_ENUMDISPLAYMODES_V1 __wire;
    __wire.index = index;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(350), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *mode = __wire.mode;
    *available = __wire.available;
    return ret;
}

/**
 * Queries the primary display, including its inactive pre-graphics state.
 * Returns UNSUPPORTED when no primary display exists.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetDisplayModeV1(struct DisplayState* state)
{
    uint32_t ret = 0;
    struct __SYS_GFX_GETDISPLAYMODE_V1 __wire;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(351), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *state = __wire.state;
    return ret;
}

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
UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetDisplayModeV1(uint32_t modeId)
{
    uint32_t ret = 0;
    struct __SYS_GFX_SETDISPLAYMODE_V1 __wire;
    __wire.modeId = modeId;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(352), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Registers a process-scoped window class; wndProc is opaque and called by userland. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_RegisterClassV1(const char* name, uint32_t wndProc, COLORREF background)
{
    uint32_t ret = 0;
    struct __SYS_USER_REGISTERCLASS_V1 __wire;
    __wire.name = name;
    __wire.nameLength = 0;
    if (name) while (name[__wire.nameLength]) __wire.nameLength++;
    __wire.wndProc = wndProc;
    __wire.background = background;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(400), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/**
 * Creates a window owned by the calling thread. Child requires a same-process
 * parent; x/y are relative to its client origin. Otherwise parent must be zero.
 * Coordinates and dimensions must fit the signed 16-bit screen range.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateWindowV1(const char* className, const char* title, enum WindowStyle style, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t parent, uint32_t userData, uint32_t* hwnd)
{
    uint32_t ret = 0;
    struct __SYS_USER_CREATEWINDOW_V1 __wire;
    __wire.className = className;
    __wire.classNameLength = 0;
    if (className) while (className[__wire.classNameLength]) __wire.classNameLength++;
    __wire.title = title;
    __wire.titleLength = 0;
    if (title) while (title[__wire.titleLength]) __wire.titleLength++;
    __wire.style = style;
    __wire.x = x;
    __wire.y = y;
    __wire.width = width;
    __wire.height = height;
    __wire.parent = parent;
    __wire.userData = userData;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(401), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *hwnd = __wire.hwnd;
    return ret;
}

/**
 * Immediately destroys the window and descendants and removes their queued
 * messages. Userland dispatches any Destroy callback before this syscall;
 * no message may subsequently target an invalidated handle.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_DestroyWindowV1(uint32_t hwnd)
{
    uint32_t ret = 0;
    struct __SYS_USER_DESTROYWINDOW_V1 __wire;
    __wire.hwnd = hwnd;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(402), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/**
 * Hide preserves state, Show preserves state, Restore restores saved geometry.
 * Minimize creates an icon plus label for children in the parent client
 * (roots retain a desktop caption). Click selects a child icon, double-click
 * or Enter restores it; Ctrl+Tab/F6 cycles including minimized child icons.
 * Maximize fills the parent client or desktop. Parent hide/minimize suppresses
 * descendant visibility.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ShowWindowV1(uint32_t hwnd, uint32_t command)
{
    uint32_t ret = 0;
    struct __SYS_USER_SHOWWINDOW_V1 __wire;
    __wire.hwnd = hwnd;
    __wire.command = command;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(403), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_MoveWindowV1(uint32_t hwnd, int32_t x, int32_t y, int32_t width, int32_t height)
{
    uint32_t ret = 0;
    struct __SYS_USER_MOVEWINDOW_V1 __wire;
    __wire.hwnd = hwnd;
    __wire.x = x;
    __wire.y = y;
    __wire.width = width;
    __wire.height = height;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(404), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Window rectangle in screen coordinates. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetWindowRectV1(uint32_t hwnd, struct Rect* rect)
{
    uint32_t ret = 0;
    struct __SYS_USER_GETWINDOWRECT_V1 __wire;
    __wire.hwnd = hwnd;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(405), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *rect = __wire.rect;
    return ret;
}

/** Adds rect (client coordinates) to the invalid area; whole != 0 invalidates the client. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_InvalidateRectV1(uint32_t hwnd, const struct Rect* rect, uint32_t whole)
{
    uint32_t ret = 0;
    struct __SYS_USER_INVALIDATERECT_V1 __wire;
    __wire.hwnd = hwnd;
    __wire.rect = *rect;
    __wire.whole = whole;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(406), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/**
 * Returns a DC clipped to the visible invalid client, origin at the client.
 * Only one paint DC per window; EndPaint must match its hwnd/DC pair.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_BeginPaintV1(uint32_t hwnd, uint32_t* dc, struct PaintInfo* paint)
{
    uint32_t ret = 0;
    struct __SYS_USER_BEGINPAINT_V1 __wire;
    __wire.hwnd = hwnd;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(407), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *dc = __wire.dc;
    *paint = __wire.paint;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_EndPaintV1(uint32_t hwnd, uint32_t dc)
{
    uint32_t ret = 0;
    struct __SYS_USER_ENDPAINT_V1 __wire;
    __wire.hwnd = hwnd;
    __wire.dc = dc;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(408), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Blocks until a message arrives; msg.message == Quit ends a message loop. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetMessageV1(struct Msg* msg)
{
    uint32_t ret = 0;
    struct __SYS_USER_GETMESSAGE_V1 __wire;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(409), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *msg = __wire.msg;
    return ret;
}

/** Non-blocking; returns TIMEOUT when the queue is empty. remove == 0 leaves it queued. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_PeekMessageV1(struct Msg* msg, uint32_t remove)
{
    uint32_t ret = 0;
    struct __SYS_USER_PEEKMESSAGE_V1 __wire;
    __wire.remove = remove;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(410), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *msg = __wire.msg;
    return ret;
}

/** Queues a message; returns OUT_OF_MEMORY when the target ring is full. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_PostMessageV1(uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam)
{
    uint32_t ret = 0;
    struct __SYS_USER_POSTMESSAGE_V1 __wire;
    __wire.hwnd = hwnd;
    __wire.message = message;
    __wire.wParam = wParam;
    __wire.lParam = lParam;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(411), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** One of WindowLongIndex. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetWindowLongPtrV1(uint32_t hwnd, uint32_t index, uint32_t* value)
{
    uint32_t ret = 0;
    struct __SYS_USER_GETWINDOWLONGPTR_V1 __wire;
    __wire.hwnd = hwnd;
    __wire.index = index;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(412), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *value = __wire.value;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetWindowLongPtrV1(uint32_t hwnd, uint32_t index, uint32_t value, uint32_t* previous)
{
    uint32_t ret = 0;
    struct __SYS_USER_SETWINDOWLONGPTR_V1 __wire;
    __wire.hwnd = hwnd;
    __wire.index = index;
    __wire.value = value;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(413), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *previous = __wire.previous;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetCaptureV1(uint32_t hwnd)
{
    uint32_t ret = 0;
    struct __SYS_USER_SETCAPTURE_V1 __wire;
    __wire.hwnd = hwnd;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(414), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_ReleaseCaptureV1(uint32_t unused)
{
    uint32_t ret = 0;
    struct __SYS_USER_RELEASECAPTURE_V1 __wire;
    __wire.unused = unused;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(415), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Client bounds {0,0,width,height}; empty while minimized. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetClientRectV1(uint32_t hwnd, struct Rect* rect)
{
    uint32_t ret = 0;
    struct __SYS_USER_GETCLIENTRECT_V1 __wire;
    __wire.hwnd = hwnd;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(416), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *rect = __wire.rect;
    return ret;
}

/**
 * Focus a visible same-process window and activate its root. A minimized
 * child icon may receive focus for Enter/Alt+Space; minimized roots cannot.
 * hwnd == 0 clears this process's focus without changing activation.
 * Key lParam: bits 0..7 scan code, 8..10 key_flags, 16..24 modifier_flags.
 * Char lParam carries the same metadata. Mouse wParam low bits: left=1,right=2;
 * modifier_flags occupy bits 16..24. Mouse lParam packs signed client x/y.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetFocusV1(uint32_t hwnd)
{
    uint32_t ret = 0;
    struct __SYS_USER_SETFOCUS_V1 __wire;
    __wire.hwnd = hwnd;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(417), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Set a same-process window title (at most 63 bytes plus terminator). */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetWindowTextV1(uint32_t hwnd, const char* text)
{
    uint32_t ret = 0;
    struct __SYS_USER_SETWINDOWTEXT_V1 __wire;
    __wire.hwnd = hwnd;
    __wire.text = text;
    __wire.textLength = 0;
    if (text) while (text[__wire.textLength]) __wire.textLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(418), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/**
 * Reports usable FPU support, not merely the presence of an x87 unit.
 * Native is only valid when the kernel initializes and preserves each thread's
 * FPU state; Emulated is only valid when an instruction emulator is active.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_QueryFpuV1(enum FpuCapability* capability)
{
    uint32_t ret = 0;
    struct __SYS_ABI_QUERYFPU_V1 __wire;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(207), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *capability = __wire.capability;
    return ret;
}

/** Reports the version of the network subsystem; UNSUPPORTED when it is not loaded. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_QueryNetworkVersionV1(uint32_t* version)
{
    uint32_t ret = 0;
    struct __SYS_NET_QUERYNETWORKVERSION_V1 __wire;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(500), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *version = __wire.version;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_PrintV1(const char* message)
{
    uint32_t ret = 0;
    struct __SYS_DEBUG_PRINT_V1 __wire;
    __wire.message = message;
    __wire.messageLength = 0;
    if (message) while (message[__wire.messageLength]) __wire.messageLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(100), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/**
 * Creates a new process from the executable at `path`. Waits only for loader
 * mapping/dependency/relocation completion, not constructors or child exit.
 * There is no timeout: the bounded loader must signal readiness or exit on
 * failure, so failure never leaves a running child without an owned handle.
 * On failure no process handle is returned.
 * Successful handles may be closed immediately without terminating the child.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateProcessV1(const char* path, const char* argument, uint32_t flags, HANDLE* handle)
{
    uint32_t ret = 0;
    struct __SYS_PROC_CREATEPROCESS_V1 __wire;
    __wire.path = path;
    __wire.pathLength = 0;
    if (path) while (path[__wire.pathLength]) __wire.pathLength++;
    __wire.argument = argument;
    __wire.argumentLength = 0;
    if (argument) while (argument[__wire.argumentLength]) __wire.argumentLength++;
    __wire.flags = flags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(200), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *handle = __wire.handle;
    return ret;
}

/**
 * Creates a process with explicit standard streams. Same path, argument,
 * flags, readiness and handle ownership contract as CreateProcessV1.
 * Only these streams are inherited; unrelated handles are not inherited.
 * Handles are caller-local open I/O objects, stdin requires read access,
 * stdout/stderr require write access. Child handles are independent copies.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateProcessV2(const char* path, const char* argument, uint32_t flags, HANDLE stdinHandle, HANDLE stdoutHandle, HANDLE stderrHandle, HANDLE* handle)
{
    uint32_t ret = 0;
    struct __SYS_PROC_CREATEPROCESS_V2 __wire;
    __wire.path = path;
    __wire.pathLength = 0;
    if (path) while (path[__wire.pathLength]) __wire.pathLength++;
    __wire.argument = argument;
    __wire.argumentLength = 0;
    if (argument) while (argument[__wire.argumentLength]) __wire.argumentLength++;
    __wire.flags = flags;
    __wire.stdinHandle = stdinHandle;
    __wire.stdoutHandle = stdoutHandle;
    __wire.stderrHandle = stderrHandle;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(208), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *handle = __wire.handle;
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

/** Blocks until a waitable handle (event or process) is signalled. Returns TIMEOUT on expiry. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_WaitForObjectV1(HANDLE handle, uint32_t timeoutMs)
{
    uint32_t ret = 0;
    struct __SYS_PROC_WAITFOROBJECT_V1 __wire;
    __wire.handle = handle;
    __wire.timeoutMs = timeoutMs;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(203), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Blocks until a process exits and returns its exit code. Returns TIMEOUT on expiry. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_WaitProcessV1(HANDLE handle, uint32_t timeoutMs, uint32_t* exitCode)
{
    uint32_t ret = 0;
    struct __SYS_PROC_WAITPROCESS_V1 __wire;
    __wire.handle = handle;
    __wire.timeoutMs = timeoutMs;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(204), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *exitCode = __wire.exitCode;
    return ret;
}

/** Creates an event object. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateEventV1(enum EventFlags flags, HANDLE* handle)
{
    uint32_t ret = 0;
    struct __SYS_PROC_CREATEEVENT_V1 __wire;
    __wire.flags = flags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(205), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *handle = __wire.handle;
    return ret;
}

/** Signals an event. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetEventV1(HANDLE handle)
{
    uint32_t ret = 0;
    struct __SYS_PROC_SETEVENT_V1 __wire;
    __wire.handle = handle;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(206), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/**
 * Allocates zeroed pages, rounding size up to 4096. Read is required; unknown
 * flags and no-access/write-only mappings reject. i386 does not enforce NX.
 */
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

/**
 * Frees a whole eager anonymous region; base must be page aligned and size,
 * rounded up to 4096, must equal the region size. No partial frees or aliases.
 */
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

/** Reserved: currently rejects with INVALID_ARGUMENT (no partial VAD updates). */
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

/**
 * Maps zeroed anonymous user pages at a page-aligned address selected by the
 * caller. Read is required; unknown flags and no-access/write-only mappings
 * reject. i386 does not enforce NX.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_MapFixedPagesV1(uint32_t address, uint32_t size, enum MemoryProtectFlags flags)
{
    uint32_t ret = 0;
    struct __SYS_MEM_MAPFIXEDPAGES_V1 __wire;
    __wire.address = address;
    __wire.size = size;
    __wire.flags = flags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(213), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_OpenV1(const char* path, enum FileOpenMode mode, HANDLE* handle)
{
    uint32_t ret = 0;
    struct __SYS_IO_OPEN_V1 __wire;
    __wire.path = path;
    __wire.pathLength = 0;
    if (path) while (path[__wire.pathLength]) __wire.pathLength++;
    __wire.mode = mode;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(220), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *handle = __wire.handle;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CloseV1(HANDLE handle)
{
    uint32_t ret = 0;
    struct __SYS_IO_CLOSE_V1 __wire;
    __wire.handle = handle;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(221), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Reads data from an open object handle into `data`. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ReadV1(HANDLE handle, uint8_t* data, uint32_t* dataLength)
{
    uint32_t ret = 0;
    struct __SYS_IO_READ_V1 __wire;
    __wire.handle = handle;
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

/** Writes data from `data` to an open object handle. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_WriteV1(HANDLE handle, const uint8_t* data, uint32_t dataLength)
{
    uint32_t ret = 0;
    struct __SYS_IO_WRITE_V1 __wire;
    __wire.handle = handle;
    __wire.data = data;
    __wire.dataLength = dataLength;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(223), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Writes bytes and reports short writes. V1 remains available unchanged. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_WriteV2(HANDLE handle, const uint8_t* data, uint32_t dataLength, uint32_t* transferred)
{
    uint32_t ret = 0;
    struct __SYS_IO_WRITE_V2 __wire;
    __wire.handle = handle;
    __wire.data = data;
    __wire.dataLength = dataLength;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(226), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *transferred = __wire.transferred;
    return ret;
}

/**
 * Enumerates one directory entry by zero-based index, excluding . and ...
 * Requires a readable directory handle. found=0 means end of directory.
 * name uses query-length-then-fill, including its terminating NUL.
 * Indices are not snapshots: restart enumeration after a mutation.
 * On insufficient capacity returns INVALID_SIZE without truncation.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ReadDirectoryV1(HANDLE handle, uint32_t index, char* name, uint32_t* nameLength, struct FileInformation* information, uint32_t* found)
{
    uint32_t ret = 0;
    struct __SYS_IO_READDIRECTORY_V1 __wire;
    __wire.handle = handle;
    __wire.index = index;
    __wire.name = name;
    __wire.nameLength = nameLength ? *nameLength : 0;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(227), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    if (nameLength) *nameLength = __wire.nameLength;
    *information = __wire.information;
    *found = __wire.found;
    return ret;
}

/** Returns information for an open VFS file/directory handle with read access. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetFileInformationV1(HANDLE handle, struct FileInformation* information)
{
    uint32_t ret = 0;
    struct __SYS_IO_GETFILEINFORMATION_V1 __wire;
    __wire.handle = handle;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(229), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *information = __wire.information;
    return ret;
}

/**
 * Copies one file to a nonexisting destination on the same mount.
 * Both paths must be absolute, at most 255 bytes, with FAT short names.
 * Open source entries and open directories block the operation.
 * Directory copies and cross-volume copies are unsupported.
 * Device failures can leave partial changes; no crash consistency is promised.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_CopyFileV1(const char* source, const char* destination)
{
    uint32_t ret = 0;
    struct __SYS_IO_COPYFILE_V1 __wire;
    __wire.source = source;
    __wire.sourceLength = 0;
    if (source) while (source[__wire.sourceLength]) __wire.sourceLength++;
    __wire.destination = destination;
    __wire.destinationLength = 0;
    if (destination) while (destination[__wire.destinationLength]) __wire.destinationLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(230), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/**
 * Moves a file, or renames a directory within the same parent.
 * Both paths must be absolute, at most 255 bytes, with FAT short names.
 * The destination must not exist and must be on the same mount.
 * Open source entries and open directories block the operation.
 * Cross-parent directory moves and cross-volume moves are unsupported.
 * Device failures can leave partial changes; no crash consistency is promised.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_MoveFileV1(const char* source, const char* destination)
{
    uint32_t ret = 0;
    struct __SYS_IO_MOVEFILE_V1 __wire;
    __wire.source = source;
    __wire.sourceLength = 0;
    if (source) while (source[__wire.sourceLength]) __wire.sourceLength++;
    __wire.destination = destination;
    __wire.destinationLength = 0;
    if (destination) while (destination[__wire.destinationLength]) __wire.destinationLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(231), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/**
 * Deletes a file or empty directory at an absolute path of at most 255 bytes.
 * Requires FAT short names. Roots, . and .. are rejected.
 * Open source entries, open directories and read-only entries block deletion.
 * Recursive deletion is unsupported. Device failures can leave partial changes;
 * no crash consistency is promised.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_DeleteFileV1(const char* path)
{
    uint32_t ret = 0;
    struct __SYS_IO_DELETEFILE_V1 __wire;
    __wire.path = path;
    __wire.pathLength = 0;
    if (path) while (path[__wire.pathLength]) __wire.pathLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(232), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/**
 * Creates a directory at a nonexisting absolute path of at most 255 bytes.
 * Requires FAT short names and an existing parent directory.
 * Open directory handles on the volume block creation.
 * Device failures can leave partial changes; no crash consistency is promised.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateDirectoryV1(const char* path)
{
    uint32_t ret = 0;
    struct __SYS_IO_CREATEDIRECTORY_V1 __wire;
    __wire.path = path;
    __wire.pathLength = 0;
    if (path) while (path[__wire.pathLength]) __wire.pathLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(233), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Sends a control code to the device behind an open handle. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ControlV1(HANDLE handle, uint32_t code, const uint8_t* input, uint32_t inputLength, uint8_t* output, uint32_t* outputLength)
{
    uint32_t ret = 0;
    struct __SYS_IO_CONTROL_V1 __wire;
    __wire.handle = handle;
    __wire.code = code;
    __wire.input = input;
    __wire.inputLength = inputLength;
    __wire.output = output;
    __wire.outputLength = outputLength ? *outputLength : 0;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(224), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    /* buffer was mapped and filled by the kernel directly; report actual length written */
    if (outputLength) *outputLength = __wire.outputLength;
    return ret;
}

/**
 * Returns the opaque handle assigned to a standard stream, or
 * INVALID_ARGUMENT if that stream is not available to the calling process.
 */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetStdHandleV1(enum StdHandleSelector selector, HANDLE* handle)
{
    uint32_t ret = 0;
    struct __SYS_IO_GETSTDHANDLE_V1 __wire;
    __wire.selector = selector;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(225), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *handle = __wire.handle;
    return ret;
}

/** Creates a paired virtual TTY returning source and sink handles. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateV1(HANDLE* sourceHandle, HANDLE* sinkHandle)
{
    uint32_t ret = 0;
    struct __SYS_TTY_CREATE_V1 __wire;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(250), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *sourceHandle = __wire.sourceHandle;
    *sinkHandle = __wire.sinkHandle;
    return ret;
}

/** Sets TTY configuration flags and window dimensions. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetConfigV1(HANDLE handle, enum TtyModeFlags flags, uint16_t rows, uint16_t cols)
{
    uint32_t ret = 0;
    struct __SYS_TTY_SETCONFIG_V1 __wire;
    __wire.handle = handle;
    __wire.flags = flags;
    __wire.rows = rows;
    __wire.cols = cols;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(251), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Retrieves TTY configuration flags and window dimensions. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetConfigV1(HANDLE handle, enum TtyModeFlags* flags, uint16_t* rows, uint16_t* cols)
{
    uint32_t ret = 0;
    struct __SYS_TTY_GETCONFIG_V1 __wire;
    __wire.handle = handle;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(252), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *flags = __wire.flags;
    *rows = __wire.rows;
    *cols = __wire.cols;
    return ret;
}

