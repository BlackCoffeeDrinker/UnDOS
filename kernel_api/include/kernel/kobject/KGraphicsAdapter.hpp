#pragma once


#include <expected.hpp>

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/event_broadcast.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/gfx/structs.hpp>
#include <kernel/kerror.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {
struct KDriverObject;
struct KDisplay;
struct KClipRect;

// What the adapter can do in hardware; drives how the kernel presents to it.
enum class KGfxCaps : uint32_t {
  None = 0,
  HwFill = 1u << 0,
  HwBlt = 1u << 1,
  DirectScanout = 1u << 2,
  Hw3D = 1u << 3,
};

constexpr KGfxCaps operator|(KGfxCaps a, KGfxCaps b) noexcept {
  return static_cast<KGfxCaps>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr KGfxCaps operator&(KGfxCaps a, KGfxCaps b) noexcept {
  return static_cast<KGfxCaps>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr bool Any(KGfxCaps a) noexcept { return static_cast<uint32_t>(a) != 0; }

struct KGraphicsAdapterDisplayEnumInfo {
  size_t displayIndex;
};

// Adapter representing the driver in charge of a piece of graphics hardware (e.g. "VGA16")
struct KGraphicsAdapter : KObjectT<KGraphicsAdapter, 1, TYPE_GRAPHICS_ADAPTER> {
  using KObjectT::KObjectT;
  enum class State : uint8_t {
    Uninitialized,
    Terminated,
    Probing,
    NeedsDisplayEnumerated,
    Running
  };

  State state;                     // current state of the adapter, handeled by the kernel
  KObjectPtr<KDriverObject> driver;// optional driver binary object
  KDevicePtr<KDevice> device;      // optional device object

  // Helper function to get the device's extension
  template<typename T>
  T *GetDeviceExtensionAs() const {
    if (!device) return nullptr;
    return device->deviceExtension.as<T>();
  }

  // Called by kernel to probe and enumerate displays
  cfunc<KernelError(const KObjectPtr<KGraphicsAdapter> &)> Probe;// optional: detect hardware

  /**
   * The graphics subsystem calls this with increasing display indices until
   * it returns KernelError::OutOfRange (no more heads). Any other error
   * aborts the enumeration of this adapter.
   */
  cfunc<KernelError(const KObjectPtr<KGraphicsAdapter> &, const KGraphicsAdapterDisplayEnumInfo &, KGraphicsAdapterDisplayInfo &)> ProbeDisplay;

  /**
   * Called once per head the driver reported from ProbeDisplay, after the
   * graphics subsystem has created the KDisplay for it and before the display
   * is inserted into the object namespace.
   *
   * The driver binds its per-head entry points on the passed display here:
   * GetModeInfo / SetMode / Enable / Disable / Validate / SetPowerState.
   *
   * Returning an error discards the head.
   */
  cfunc<KernelError(const KObjectPtr<KGraphicsAdapter> &, const KObjectPtr<KDisplay> &display)> ConfigureDisplay;

  // Basic 2D operations. KernelError::Unsupported asks the kernel to use its
  // software fallback; any other error means the request itself failed.
  cfunc<KernelError(const KObjectPtr<KGraphicsAdapter> &,
             const KObjectPtr<KSurface> &dst,
             int dstX, int dstY,
             const KObjectPtr<KSurface> &src,
             int srcX, int srcY,
             uint32_t width, uint32_t height,
             RasterOp rop)>
      BitBlt;

  cfunc<KernelError(const KObjectPtr<KGraphicsAdapter> &,
             const KObjectPtr<KSurface> &dst,
             int x, int y, int w, int h,
             uint32_t colorIndex)>
      FillRect;

  cfunc<KernelError(const KObjectPtr<KGraphicsAdapter> &,
             const KObjectPtr<KSurface> &dst,
             int x0, int y0, int x1, int y1,
             uint32_t colorIndex)>
      DrawLine;

  // Optional native-pixel readback for saved-under cursors on non-linear
  // device surfaces. Returns the native pixel value.
  cfunc<kstd::expected<uint32_t, KernelError>(const KObjectPtr<KGraphicsAdapter> &,
                                              const KObjectPtr<KSurface> &src, int x, int y)>
      ReadPixel;

  // Optional present / flush hook. `rects` is a list of `count` screen
  // rectangles to transfer; nullptr / count == 0 means the whole surface.
  cfunc<void(const KObjectPtr<KGraphicsAdapter> &,
             const KObjectPtr<KSurface> &surface, const KClipRect *rects, size_t count)>
      Present;

  KGfxCaps caps = KGfxCaps::None;

  // Interest in the backing device's state changes, registered by
  // KE_GFX_RegisterGraphicsAdapter. Held here so the registration lives exactly
  // as long as the adapter object does.
  event::EventInterestToken deviceStateInterest{};
};

template<>
struct ObjectTypeOf<TYPE_GRAPHICS_ADAPTER> {
  using type = KGraphicsAdapter;
};
}// namespace kernel
