#pragma once

#include <kernel/__core.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {
struct KMemoryBackingObject;

// 1. Enums declared first
enum class PixelFormat : uint8_t {
  RGB24,
  ARGB32,
  INDEX8,
};

enum class RasterOp : uint8_t {
  SRCCOPY,
  SRCAND,
  SRCINVERT,
  SRCERASE,
  NOTSRCCOPY,
};

// 2. Display Mode Information Struct
struct KDisplayModeInfo {
  uint32_t width = 0;                          // Visible horizontal resolution in pixels
  uint32_t height = 0;                         // Visible vertical resolution in pixels
  uint32_t pitch = 0;                          // Scanline byte stride (bytes per row)
  uint32_t bpp = 0;                            // Bits per pixel (e.g., 8, 16, 24, 32)
  uint32_t refreshRate = 0;                    // Vertical refresh rate in Hz (0 if default/unspecified)
  PixelFormat pixelFormat = PixelFormat::RGB24;// Color arrangement / depth format

  bool interlaced{false};
  bool hwAccelerated{false};
  bool textMode{false};
};

// Backing memory abstraction for a framebuffer or offscreen surface.
struct KSurface : KObjectT<KSurface, 1, TYPE_SURFACE> {
  KObjectPtr<KMemoryBackingObject> backing;// Memory backing the surface's pixel data

  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t pitch = 0;// Bytes per scanline
  PixelFormat pixelFormat = PixelFormat::RGB24;
};

template<>
struct ObjectTypeOf<TYPE_SURFACE> {
  using type = KSurface;
};
}// namespace kernel
