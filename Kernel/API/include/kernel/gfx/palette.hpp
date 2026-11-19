#pragma once

#include <stdint.h>

namespace kernel {

// A surface color table: 0x00RRGGBB per index. Entries 0..15 are the palette16
// colors USER draws with; drivers with a hardware CLUT program it from here.
struct KPalette {
  static constexpr uint32_t kSize = 256;
  uint32_t rgb[kSize];
};

// Default table: the 16 standard VGA colors, the remaining entries black.
constexpr KPalette KDefaultPalette() {
  KPalette palette{};
  constexpr uint32_t vga[16] = {
      0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
      0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
      0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
      0xff5555, 0xff55ff, 0xffff55, 0xffffff};
  for (uint32_t i = 0; i < 16; ++i) palette.rgb[i] = vga[i];
  return palette;
}

} // namespace kernel
