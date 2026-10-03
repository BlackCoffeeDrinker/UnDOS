#pragma once

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <kernel/event_broadcast.hpp>

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
  cfunc<bool(const KObjectPtr<KGraphicsAdapter> &)> Probe;            // optional: detect hardware
  cfunc<bool(const KObjectPtr<KGraphicsAdapter> &)> EnumerateDisplays;// populates child KDisplay objects (Call KE_GFX_RegisterDisplay for new displays and KE_GFX_UnregisterDisplay for displays that have been removed)

  // Mode and surface management
  cfunc<bool(const KObjectPtr<KGraphicsAdapter> &, const KObjectPtr<KDisplay> &display, const KObjectPtr<KSurface> &, size_t modeIndex)> SetMode;
  cfunc<void(const KObjectPtr<KGraphicsAdapter> &, const KObjectPtr<KDisplay> &display)> UnsetMode;

  // Basic 2D operations (driver may return false to indicate kernel fallback)
  cfunc<bool(const KObjectPtr<KGraphicsAdapter> &,
             const KObjectPtr<KSurface> &dst,
             int dstX, int dstY,
             const KObjectPtr<KSurface> &src,
             int srcX, int srcY,
             uint32_t width, uint32_t height,
             RasterOp rop)>
      BitBlt;

  cfunc<bool(const KObjectPtr<KGraphicsAdapter> &,
             const KObjectPtr<KSurface> &dst,
             int x, int y, int w, int h,
             uint32_t colorIndex)>
      FillRect;

  cfunc<bool(const KObjectPtr<KGraphicsAdapter> &,
             const KObjectPtr<KSurface> &dst,
             int x0, int y0, int x1, int y1,
             uint32_t colorIndex)>
      DrawLine;

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
