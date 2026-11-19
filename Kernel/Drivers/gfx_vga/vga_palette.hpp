#pragma once

#include <kernel/gfx/palette.hpp>

namespace vga_regs {

// Loads DAC entries [first, first + count) from `palette`. Attribute palette
// entries map 0..15 directly to DAC entries 0..15. Firmware can leave any
// palette behind, so a mode set must reload it explicitly.
template<typename WritePort>
void ProgramDac(WritePort &&write, const kernel::KPalette &palette, uint32_t first, uint32_t count) {
  if (first >= kernel::KPalette::kSize) return;
  if (count > kernel::KPalette::kSize - first) count = kernel::KPalette::kSize - first;
  write(uint16_t{0x3c6}, uint8_t{0xff});                    // DAC pixel mask
  write(uint16_t{0x3c8}, static_cast<uint8_t>(first));       // first DAC entry
  for (uint32_t index = first; index < first + count; ++index) {
    const uint32_t rgb = palette.rgb[index];
    // VGA DAC channels are six bits: 8-bit levels keep their top six bits.
    write(uint16_t{0x3c9}, static_cast<uint8_t>((rgb >> 18) & 63));
    write(uint16_t{0x3c9}, static_cast<uint8_t>((rgb >> 10) & 63));
    write(uint16_t{0x3c9}, static_cast<uint8_t>((rgb >> 2) & 63));
  }
}

} // namespace vga_regs
