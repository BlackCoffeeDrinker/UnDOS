#pragma once

#include <kernel/__core.hpp>
#include <kernel/fwd/KDisplay.hpp>
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
  RGB15,// 5-5-5, 2 bytes per pixel
  RGB16,// 5-6-5, 2 bytes per pixel
};

enum class RasterOp : uint8_t {
  SRCCOPY,
  SRCAND,
  SRCINVERT,
  SRCERASE,
  NOTSRCCOPY,
};

// 2. Display Mode Information Struct
namespace DisplayModeFlags {
constexpr uint32_t NonStandard = (1 << 0);        // Non-standard video mode timing
constexpr uint32_t TextMode = (1 << 1);           // Hardware text mode
constexpr uint32_t GraphicsMode = (1 << 2);       // Graphics mode
constexpr uint32_t ColorMode = (1 << 3);          // Color display (vs monochrome)
constexpr uint32_t Interlaced = (1 << 4);         // Interlaced video signal
constexpr uint32_t PaletteDriven = (1 << 5);      // Uses hardware palette/CLUT (e.g., INDEX8)
constexpr uint32_t HardwareAccelerated = (1 << 6);// Supports 2D/3D hardware acceleration
constexpr uint32_t Banked = (1 << 7);             // Banked/segmented framebuffer (e.g., VBE 2.0)
}// namespace DisplayModeFlags

struct KDisplayModeInfo {
  bool operator==(const KDisplayModeInfo &) const = default;
  // Mode Identification & Target Geometry
  size_t modeIndex{0};// Driver mode ID (Win2k ModeIndex)
  uint32_t width{0};    // Active horizontal pixels (Win2k VisScreenWidth)
  uint32_t height{0};   // Active vertical scanlines (Win2k VisScreenHeight)
  uint32_t pitch{0};    // Scanline byte stride (Win2k ScreenStride)

  // Addressable Video Memory Bounds (Panning / Offscreen VRAM)
  uint32_t videoMemoryBitmapWidth{0}; // Addressable offscreen width in pixels
  uint32_t videoMemoryBitmapHeight{0};// Addressable offscreen height in pixels

  // Color Depth & Planar Architecture
  uint32_t bpp{0};           // Effective bits per pixel (numberOfPlanes * bitsPerPlane)
  uint32_t numberOfPlanes{1};// Memory planes (e.g. 4 for planar VGA, 1 for linear)
  uint32_t bitsPerPlane{0};  // Bits per plane per pixel

  // Hardware Timings & Physical Metrics
  uint32_t refreshRate{0};// Refresh rate in Hz
  uint32_t xMillimeter{0};// Active display physical width in mm (from EDID)
  uint32_t yMillimeter{0};// Active display physical height in mm (from EDID)

  // Color Format & Direct-Color Bitmasks
  PixelFormat pixelFormat{PixelFormat::RGB24};// UnDOS format enumeration
  uint32_t numberRedBits{0};                  // Color channel bit count (Red)
  uint32_t numberGreenBits{0};                // Color channel bit count (Green)
  uint32_t numberBlueBits{0};                 // Color channel bit count (Blue)
  uint32_t redMask{0};                        // Bitmask for Red channel (e.g., 0x00FF0000)
  uint32_t greenMask{0};                      // Bitmask for Green channel (e.g., 0x0000FF00)
  uint32_t blueMask{0};                       // Bitmask for Blue channel (e.g., 0x000000FF)

  // Mode Attributes & Custom Extensions
  uint32_t attributeFlags{0};              // Bitfield of DisplayModeFlags
  uint32_t driverSpecificAttributeFlags{0};// Private driver-defined attribute flags

  // Convenience Helpers for Backward Compatibility
  [[nodiscard]] constexpr bool isTextMode() const { return (attributeFlags & DisplayModeFlags::TextMode) != 0; }
  [[nodiscard]] constexpr bool isInterlaced() const { return (attributeFlags & DisplayModeFlags::Interlaced) != 0; }
  [[nodiscard]] constexpr bool isHwAccelerated() const { return (attributeFlags & DisplayModeFlags::HardwareAccelerated) != 0; }
};

enum class SurfaceType : uint8_t {
  OffscreenBitmap,// Engine/RAM DIB section (Win2k STYPE_BITMAP)
  Framebuffer,    // Physical display output hardware surface (Win2k STYPE_DEVICE)
};

namespace SurfaceFlags {
constexpr uint32_t TopDown = (1 << 0);   // Scanline 0 is top (positive pitch)
constexpr uint32_t Linear = (1 << 1);    // Linear memory layout (vs banked/planar)
constexpr uint32_t UserMapped = (1 << 2);// Buffer mapped into userland address space
}// namespace SurfaceFlags

// Backing memory abstraction for a framebuffer or offscreen surface.
struct KSurface : KObjectT<KSurface, 1, TYPE_SURFACE> {
  using KObjectT::KObjectT;

  // Hardware & Device Context Binding
  KObjectPtr<KMemoryBackingObject> backing;      // Memory backing the surface's pixel data
  KObjectPtr<KDisplay> display{nullptr};         // Owning display head (null for offscreen DIBs)
  SurfaceType type{SurfaceType::OffscreenBitmap};// System RAM vs Hardware VRAM

  // Geometry & Layout (directly populated from KDisplayModeInfo)
  uint32_t width{0};                          // Width in pixels
  uint32_t height{0};                         // Height in pixels
  int32_t pitch{0};                           // Scanline byte stride (Win2k lDelta)
  uint32_t bpp{0};                            // Bits per pixel
  size_t sizeInBytes{0};                      // Total buffer byte size (Win2k cjBits)
  PixelFormat pixelFormat{PixelFormat::RGB24};// Explicit pixel layout enum

  // Locking & Flags
  uint32_t flags{0};   // Bitfield of SurfaceFlags
  bool isLocked{false};// Set during userland LockSurface calls
};

template<>
struct ObjectTypeOf<TYPE_SURFACE> {
  using type = KSurface;
};
}// namespace kernel
