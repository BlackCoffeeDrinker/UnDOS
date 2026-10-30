#pragma once

#include <kernel/kobject/KSurface.hpp>

namespace kernel::gfx::vga {
constexpr uint32_t ModeWidth = 640;
constexpr uint32_t ModeHeight = 480;
constexpr uint32_t ModePitch = ModeWidth / 8;

inline bool GetModeInfo(size_t modeIndex, KDisplayModeInfo &outInfo) {
  if (modeIndex != 0) return false;
  outInfo = KDisplayModeInfo{
      .modeIndex = 0,
      .width = ModeWidth,
      .height = ModeHeight,
      .pitch = ModePitch,
      .videoMemoryBitmapWidth = ModeWidth,
      .videoMemoryBitmapHeight = ModeHeight,
      .bpp = 4,
      .numberOfPlanes = 4,
      .bitsPerPlane = 1,
      .refreshRate = 60,
      .pixelFormat = PixelFormat::INDEX8,
      .attributeFlags = DisplayModeFlags::GraphicsMode |
                        DisplayModeFlags::ColorMode |
                        DisplayModeFlags::PaletteDriven};
  return true;
}
}// namespace kernel::gfx::vga
