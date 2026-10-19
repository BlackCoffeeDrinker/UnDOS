
#pragma once
#include <expected.hpp>

#include <kernel/KObject.hpp>
#include <kernel/kerror.hpp>

namespace kernel {
using KGraphicsAdapterResult = kstd::expected<KObjectPtr<KGraphicsAdapter>, KernelError>;
using KSurfaceResult = kstd::expected<KObjectPtr<KSurface>, KernelError>;
}// namespace kernel

/**
 * @ingroup GFX
 * @brief Method KE_GFX_RegisterGraphicsAdapter
 *
 * Creates the adapter object and inserts it into the GFX directory. Fails
 * with OutOfMemory when the object cannot be allocated, or with the object
 * manager's insertion error.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KGraphicsAdapterResult,
    KE_GFX_RegisterGraphicsAdapter,
    const kernel::KObjectPtr<kernel::KDriverObject> &driver,
    const kernel::KDevicePtr<kernel::KDevice> &backingDevice,
    const kstd::string_view &name);

/**
 * @ingroup GFX
 * @brief Method KE_GFX_CreateSurface
 *
 * Programs `mode` (see KE_GFX_SetDisplayMode) and returns the display surface.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KSurfaceResult,
    KE_GFX_CreateSurfaceForMode,
    const kernel::KObjectPtr<kernel::KDisplay> &display, size_t mode);

/**
 * @ingroup GFX
 * @brief Programs `modeIndex` on `display`, rebinding display->surface in
 * place so existing device contexts stay valid.
 *
 * The subsystem orchestrates the switch as display->Disable, display->SetMode,
 * surface rebind, then display->Enable. Callers serialize against USER drawing.
 * Validation/allocation failure leaves the previous mode intact. Enable failure
 * returns the driver's error with isAttachedToDesktop cleared; the programmed
 * surface remains valid and modeGeneration records the change.
 *
 * Errors: InvalidArgument (null display), Unsupported (no mode hooks, or the
 * monitor/USER cannot use the mode), OutOfRange (no such mode), OutOfMemory
 * (surface object), or the driver's SetMode/Enable error.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_GFX_SetDisplayMode,
    const kernel::KObjectPtr<kernel::KDisplay> &display, size_t modeIndex);

/**
 * @ingroup GFX
 * @brief Returns the first KDisplay the graphics subsystem created while
 * probing adapters (module-level singleton), i.e. the display GDI-style
 * syscalls (CreateDC et al.) target when no specific device is requested.
 * Returns null if no display has been created yet.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDisplay>,
    KE_GFX_GetPrimaryDisplay);

/**
 * @ingroup GFX
 * @brief Software (kernel-provided) fallback for KGraphicsAdapter::FillRect.
 * Used when a driver either doesn't implement FillRect or reports
 * Unsupported for a particular request. Writes directly into `dst`'s backing
 * memory, so `dst->backing` must already be resident
 * (KMemoryBackingObject::directAddress set); otherwise fails with Unsupported.
 * A non-positive width/height fails with InvalidSize.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_GFX_SoftwareFillRect,
    const kernel::KObjectPtr<kernel::KSurface> &dst,
    int x, int y, int w, int h,
    uint32_t colorIndex);

/**
 * @ingroup GFX
 * @brief Software (kernel-provided) fallback for KGraphicsAdapter::DrawLine.
 * Fails with Unsupported for non CPU-addressable surfaces and OutOfRange for
 * unrepresentable endpoints.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_GFX_SoftwareDrawLine,
    const kernel::KObjectPtr<kernel::KSurface> &dst,
    int x0, int y0, int x1, int y1,
    uint32_t colorIndex);

/**
 * @ingroup GFX
 * @brief Software (kernel-provided) fallback for KGraphicsAdapter::BitBlt.
 * Only RasterOp::SRCCOPY is supported; anything else, a non CPU-addressable
 * surface, or an incompatible overlapping layout fails with Unsupported.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_GFX_SoftwareBitBlt,
    const kernel::KObjectPtr<kernel::KSurface> &dst,
    int dstX, int dstY,
    const kernel::KObjectPtr<kernel::KSurface> &src,
    int srcX, int srcY,
    uint32_t width, uint32_t height,
    kernel::RasterOp rop);
