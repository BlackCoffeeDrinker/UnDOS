#pragma once

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {
struct KDriverObject;
struct KDisplay;

// Adapter representing the driver in charge of a piece of graphics hardware (e.g. "VGA16")
struct KGraphicsAdapter : KObjectT<KGraphicsAdapter, 1, TYPE_GRAPHICS_ADAPTER> {
  using KObjectT::KObjectT;
  KObjectPtr<KDriverObject> driver; // optional driver binary object
  KDevicePtr<KDevice> device; // optional device object

  // Called by kernel to probe and enumerate displays
  cfunc<bool()> Probe;            // optional: detect hardware
  cfunc<bool()> EnumerateDisplays;// populates child KDisplay objects

  // Mode and surface management
  cfunc<bool(const KObjectPtr<KDisplay> &display, const KObjectPtr<KSurface>&, size_t modeIndex)> SetMode;
  cfunc<void(const KObjectPtr<KDisplay> &display)> UnsetMode;

  // Basic 2D operations (driver may return false to indicate kernel fallback)
  cfunc<bool(const KObjectPtr<KSurface> &dst,
             int dstX, int dstY,
             const KObjectPtr<KSurface> &src,
             int srcX, int srcY,
             uint32_t width, uint32_t height,
             RasterOp rop)>
      BitBlt;

  cfunc<bool(const KObjectPtr<KSurface> &dst,
             int x, int y, int w, int h,
             uint32_t colorIndex)>
      FillRect;

  cfunc<bool(const KObjectPtr<KSurface> &dst,
             int x0, int y0, int x1, int y1,
             uint32_t colorIndex)>
      DrawLine;

  // Optional present / flush hook
  cfunc<void(const KObjectPtr<KSurface> &surface)> Present;
};

template<>
struct ObjectTypeOf<TYPE_GRAPHICS_ADAPTER> {
  using type = KGraphicsAdapter;
};
}// namespace kernel
