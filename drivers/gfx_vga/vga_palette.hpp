#pragma once

#include <kernel/gfx/bitmap_font.hpp>

namespace vga_regs {

// Attribute palette entries map 0..15 directly to these DAC entries. Firmware
// can leave any palette behind, so a mode set must initialize it explicitly.
template<typename WritePort>
void ProgramPalette16(WritePort &&write) {
  write(uint16_t{0x3c6}, uint8_t{0xff});// DAC pixel mask
  write(uint16_t{0x3c8}, uint8_t{0});   // first DAC entry
  for (uint32_t index = 0; index < 16; ++index) {
    const uint32_t rgb = kernel::gfx::Palette16Rgb(index);
    // VGA DAC channels are six bits. The canonical VGA levels 00/55/aa/ff
    // become exactly 0/21/42/63, including bright white at palette index15.
    write(uint16_t{0x3c9}, static_cast<uint8_t>((rgb >> 18) & 63));
    write(uint16_t{0x3c9}, static_cast<uint8_t>((rgb >> 10) & 63));
    write(uint16_t{0x3c9}, static_cast<uint8_t>((rgb >> 2) & 63));
  }
}

} // namespace vga_regs
