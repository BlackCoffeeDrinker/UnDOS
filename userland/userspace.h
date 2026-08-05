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
    __wire.driverNameLength = driverName ? 1 : 0;
     while (driverName[__wire.driverNameLength]) __wire.driverNameLength++;
    __wire.deviceName = deviceName;
    __wire.deviceNameLength = deviceName ? 1 : 0;
     while (deviceName[__wire.deviceNameLength]) __wire.deviceNameLength++;
    __wire.initFlags = initFlags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(300), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *handle = __wire.handle;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_DeleteDCV1(uint32_t dc)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SelectObjectV1(uint32_t dc, uint32_t object)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreatePenV1(enum PenStyle style, uint32_t width, uint32_t color)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateBrushV1(enum BrushPattern pattern, uint32_t color)
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
    __wire.nameLength = name ? 1 : 0;
     while (name[__wire.nameLength]) __wire.nameLength++;

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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_DeleteObjectV1(uint32_t object)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_MoveToV1(uint32_t dc, int32_t x, int32_t y)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_LineToV1(uint32_t dc, int32_t x, int32_t y)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_RectangleV1(uint32_t dc, int32_t left, int32_t top, int32_t right, int32_t bottom)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_EllipseV1(uint32_t dc, int32_t left, int32_t top, int32_t right, int32_t bottom)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_TextOutV1(uint32_t dc, int32_t x, int32_t y, const char* text)
{
    uint32_t ret = 0;
    struct __SYS_GFX_TEXTOUT_V1 __wire;
    __wire.dc = dc;
    __wire.x = x;
    __wire.y = y;
    __wire.text = text;
    __wire.textLength = text ? 1 : 0;
     while (text[__wire.textLength]) __wire.textLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(314), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_BitBltV1(uint32_t dstDC, int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t srcDC, int32_t srcX, int32_t srcY, enum RasterOp rop)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_StretchBltV1(uint32_t dstDC, int32_t x, int32_t y, uint32_t dstWidth, uint32_t dstHeight, uint32_t srcDC, int32_t srcX, int32_t srcY, uint32_t srcWidth, uint32_t srcHeight, enum RasterOp rop)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_AlphaBlendV1(uint32_t dstDC, uint32_t srcDC, const struct Rect* dstRect, const struct Rect* srcRect, uint8_t alpha)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SelectClipRegionV1(uint32_t dc, uint32_t region)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CombineRegionV1(uint32_t dest, uint32_t src1, uint32_t src2, enum RegionCombineMode mode)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetViewportOrgV1(uint32_t dc, int32_t x, int32_t y)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetWindowOrgV1(uint32_t dc, int32_t x, int32_t y)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetTextMetricsV1(uint32_t dc, struct DeviceCaps* metrics)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetBitmapBitsV1(uint32_t bitmap, uint8_t* buffer, uint32_t* bufferLength)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_SetBitmapBitsV1(uint32_t bitmap, const uint8_t* buffer, uint32_t bufferLength)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_GetDeviceCapsV1(uint32_t dc, struct DeviceCaps* caps)
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_PrintV1(const char* message)
{
    uint32_t ret = 0;
    struct __SYS_DEBUG_PRINT_V1 __wire;
    __wire.message = message;
    __wire.messageLength = message ? 1 : 0;
     while (message[__wire.messageLength]) __wire.messageLength++;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(100), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Creates a new process from the executable at `path`. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_CreateProcessV1(const char* path, const char* argument, uint32_t flags)
{
    uint32_t ret = 0;
    struct __SYS_PROC_CREATEPROCESS_V1 __wire;
    __wire.path = path;
    __wire.pathLength = path ? 1 : 0;
     while (path[__wire.pathLength]) __wire.pathLength++;
    __wire.argument = argument;
    __wire.argumentLength = argument ? 1 : 0;
     while (argument[__wire.argumentLength]) __wire.argumentLength++;
    __wire.flags = flags;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(200), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

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

/** Allocates `size` bytes of pages with the given protection. */
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

/** Frees a previously allocated page range. */
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

/** Changes the protection of an existing page range (e.g. mark executable). */
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

UNDOS_SYSCALL_DEF uint32_t UD_SYS_OpenV1(const char* path, enum FileOpenMode mode, uint32_t* fileDescriptor)
{
    uint32_t ret = 0;
    struct __SYS_FS_OPEN_V1 __wire;
    __wire.path = path;
    __wire.pathLength = path ? 1 : 0;
     while (path[__wire.pathLength]) __wire.pathLength++;
    __wire.mode = mode;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(220), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    *fileDescriptor = __wire.fileDescriptor;
    return ret;
}

UNDOS_SYSCALL_DEF uint32_t UD_SYS_CloseV1(uint32_t fileDescriptor)
{
    uint32_t ret = 0;
    struct __SYS_FS_CLOSE_V1 __wire;
    __wire.fileDescriptor = fileDescriptor;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(221), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

/** Reads data from an open file descriptor into `data`. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_ReadV1(uint32_t fileDescriptor, uint8_t* data, uint32_t* dataLength)
{
    uint32_t ret = 0;
    struct __SYS_FS_READ_V1 __wire;
    __wire.fileDescriptor = fileDescriptor;
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

/** Writes data from `data` to an open file descriptor. */
UNDOS_SYSCALL_DEF uint32_t UD_SYS_WriteV1(uint32_t fileDescriptor, const uint8_t* data, uint32_t dataLength)
{
    uint32_t ret = 0;
    struct __SYS_FS_WRITE_V1 __wire;
    __wire.fileDescriptor = fileDescriptor;
    __wire.data = data;
    __wire.dataLength = dataLength;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(223), "b"(sizeof __wire), "c"(&__wire), "d"(0)
        : "memory");

    return ret;
}

