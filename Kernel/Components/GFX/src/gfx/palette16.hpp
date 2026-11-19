#pragma once

#include <kernel/gfx/palette.hpp>
#include <stdint.h>

namespace kernel::gfx {

// Palette indices, not COLORREF/Win32 BGR: the same 16 VGA colors used by USER.
// Default colors; a surface's own table is in KSurface::palette.
constexpr uint32_t Palette16Rgb(uint32_t index) {
  return KDefaultPalette().rgb[index & 15];
}

constexpr uint32_t RgbTo555(uint32_t rgb) {
  return ((rgb >> 9) & 0x7c00) | ((rgb >> 6) & 0x03e0) | ((rgb >> 3) & 0x001f);
}

constexpr uint32_t RgbTo565(uint32_t rgb) {
  return ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
}

// Cirrus packed INDEX8 programs a 3:3:2 DAC ramp; planar VGA uses palette16.
constexpr uint32_t RgbTo332(uint32_t rgb) {
  return ((rgb >> 16) & 0xe0) | ((rgb >> 11) & 0x1c) | ((rgb >> 6) & 3);
}

// Packs palette entry `index` of `palette` for a surface of `bitsPerPixel`.
// Indexed (8bpp and planar) surfaces keep their own encodings.
constexpr uint32_t PaletteScreenColor(const KPalette &palette, uint32_t index, uint32_t bitsPerPixel) {
  const auto rgb = palette.rgb[index % KPalette::kSize];
  if (bitsPerPixel == 8) return RgbTo332(rgb);
  if (bitsPerPixel == 15) return RgbTo555(rgb);
  if (bitsPerPixel == 16) return RgbTo565(rgb);
  if (bitsPerPixel == 24) return rgb;
  if (bitsPerPixel == 32) return 0xff000000u | rgb;
  return index;
}

constexpr uint32_t Palette16ScreenColor(uint32_t index, uint32_t bitsPerPixel) {
  return PaletteScreenColor(KDefaultPalette(), index & 15, bitsPerPixel);
}

constexpr uint32_t RgbToPalette16(uint32_t rgb) {
  uint32_t best = 0;
  uint32_t distance = UINT32_MAX;
  for (uint32_t i = 0; i < 16; ++i) {
    const auto candidate = Palette16Rgb(i);
    const int32_t dr = static_cast<int32_t>((rgb >> 16) & 255) - static_cast<int32_t>((candidate >> 16) & 255);
    const int32_t dg = static_cast<int32_t>((rgb >> 8) & 255) - static_cast<int32_t>((candidate >> 8) & 255);
    const int32_t db = static_cast<int32_t>(rgb & 255) - static_cast<int32_t>(candidate & 255);
    const auto d = static_cast<uint32_t>(dr * dr + dg * dg + db * db);
    if (d < distance) {
      best = i;
      distance = d;
    }
  }
  return best;
}

} // namespace kernel::gfx
