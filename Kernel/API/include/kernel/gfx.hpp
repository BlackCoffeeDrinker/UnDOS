
#pragma once
#include <expected.hpp>

#include <kernel/KObject.hpp>
#include <kernel/kerror.hpp>

namespace kernel {
using KGraphicsAdapterResult = kstd::expected<KObjectPtr<KGraphicsAdapter>, kstd::error_code>;
using KSurfaceResult = kstd::expected<KObjectPtr<KSurface>, kstd::error_code>;
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

// Call outside driver programming and USER/topology locks, in preemptible
// context, after connector/capability/mode/resource changes. Re-probes known
// connectors while preserving their identities and invalidates old snapshots.
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_GFX_RefreshAdapter,
    const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter);

// PnP stop/removal retires targets; outstanding tokens keep objects alive but
// become stale. A subsequent adapter start creates new target identities.
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_GFX_RemoveAdapter,
    const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter);

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
    kstd::error_code,
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
    kstd::error_code,
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
    kstd::error_code,
    KE_GFX_SoftwareBitBlt,
    const kernel::KObjectPtr<kernel::KSurface> &dst,
    int dstX, int dstY,
    const kernel::KObjectPtr<kernel::KSurface> &src,
    int srcX, int srcY,
    uint32_t width, uint32_t height,
    kernel::RasterOp rop);
