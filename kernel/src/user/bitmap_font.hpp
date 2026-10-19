#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kernel::gfx {

inline constexpr uint32_t kFontCellWidth = 8;
inline constexpr uint32_t kFontCellHeight = 16;
inline constexpr uint32_t kFontAscent = 14;
inline constexpr uint32_t kFontDescent = 2;
inline constexpr uint32_t kFontFirstChar = 32;
inline constexpr uint32_t kFontLastChar = 126;
inline constexpr uint32_t kFontDefaultChar = '?';

// Original UnDOS 5x7 artwork, drawn for this implementation. No external font
// data was used. Each row's low five bits run left (bit 4) to right (bit 0).
inline constexpr uint8_t kBitmapGlyphs[95][7] = {
    {0,0,0,0,0,0,0},          // space
    {4,4,4,4,4,0,4},          // !
    {10,10,10,0,0,0,0},       // "
    {10,31,10,10,31,10,0},    // #
    {4,15,20,14,5,30,4},      // $
    {25,26,2,4,8,11,19},      // %
    {12,18,20,8,21,18,13},    // &
    {4,4,8,0,0,0,0},          // '
    {2,4,8,8,8,4,2},          // (
    {8,4,2,2,2,4,8},          // )
    {0,21,14,31,14,21,0},     // *
    {0,4,4,31,4,4,0},         // +
    {0,0,0,0,0,4,8},          // ,
    {0,0,0,31,0,0,0},         // -
    {0,0,0,0,0,0,4},          // .
    {1,2,2,4,8,8,16},         // /
    {14,17,19,21,25,17,14},   // 0
    {4,12,4,4,4,4,14},        // 1
    {14,17,1,2,4,8,31},       // 2
    {30,1,1,14,1,1,30},       // 3
    {2,6,10,18,31,2,2},       // 4
    {31,16,16,30,1,1,30},     // 5
    {6,8,16,30,17,17,14},     // 6
    {31,1,2,4,8,8,8},         // 7
    {14,17,17,14,17,17,14},   // 8
    {14,17,17,15,1,2,12},     // 9
    {0,0,4,0,0,4,0},          // :
    {0,0,4,0,0,4,8},          // ;
    {1,2,4,8,4,2,1},          // <
    {0,0,31,0,31,0,0},        // =
    {16,8,4,2,4,8,16},        // >
    {14,17,1,2,4,0,4},        // ?
    {14,17,23,21,23,16,14},   // @
    {4,10,17,17,31,17,17},    // A
    {30,17,17,30,17,17,30},   // B
    {14,17,16,16,16,17,14},   // C
    {28,18,17,17,17,18,28},   // D
    {31,16,16,30,16,16,31},   // E
    {31,16,16,30,16,16,16},   // F
    {14,17,16,23,17,17,15},   // G
    {17,17,17,31,17,17,17},   // H
    {14,4,4,4,4,4,14},        // I
    {7,2,2,2,18,18,12},       // J
    {17,18,20,24,20,18,17},   // K
    {16,16,16,16,16,16,31},   // L
    {17,27,21,21,17,17,17},   // M
    {17,25,21,19,17,17,17},   // N
    {14,17,17,17,17,17,14},   // O
    {30,17,17,30,16,16,16},   // P
    {14,17,17,17,21,18,13},   // Q
    {30,17,17,30,20,18,17},   // R
    {15,16,16,14,1,1,30},     // S
    {31,4,4,4,4,4,4},         // T
    {17,17,17,17,17,17,14},   // U
    {17,17,17,17,17,10,4},    // V
    {17,17,17,21,21,27,17},   // W
    {17,17,10,4,10,17,17},    // X
    {17,17,10,4,4,4,4},       // Y
    {31,1,2,4,8,16,31},       // Z
    {14,8,8,8,8,8,14},        // [
    {16,8,8,4,2,2,1},         // backslash
    {14,2,2,2,2,2,14},        // ]
    {4,10,17,0,0,0,0},        // ^
    {0,0,0,0,0,0,31},         // _
    {8,4,2,0,0,0,0},          // `
    {0,0,14,1,15,17,15},      // a
    {16,16,22,25,17,17,30},   // b
    {0,0,14,17,16,17,14},     // c
    {1,1,13,19,17,17,15},     // d
    {0,0,14,17,31,16,14},     // e
    {6,9,8,28,8,8,8},         // f
    {0,14,17,17,15,1,14},     // g
    {16,16,22,25,17,17,17},   // h
    {4,0,12,4,4,4,14},        // i
    {2,0,6,2,2,18,12},        // j
    {16,16,18,20,24,20,18},   // k
    {12,4,4,4,4,4,14},        // l
    {0,0,26,21,21,21,21},     // m
    {0,0,22,25,17,17,17},     // n
    {0,0,14,17,17,17,14},     // o
    {0,30,17,17,30,16,16},    // p
    {0,15,17,17,15,1,1},      // q
    {0,0,22,25,16,16,16},     // r
    {0,0,15,16,14,1,30},      // s
    {8,8,28,8,8,9,6},         // t
    {0,0,17,17,17,19,13},     // u
    {0,0,17,17,17,10,4},      // v
    {0,0,17,17,21,21,10},     // w
    {0,0,17,10,4,10,17},      // x
    {0,17,17,17,15,1,14},     // y
    {0,0,31,2,4,8,31},        // z
    {2,4,4,8,4,4,2},          // {
    {4,4,4,4,4,4,4},          // |
    {8,4,4,2,4,4,8},          // }
    {0,0,8,21,2,0,0},         // ~
};

constexpr uint8_t BitmapGlyphRow(uint8_t character, uint32_t row) {
  if (row >= 7) return 0;
  if (character < kFontFirstChar || character > kFontLastChar) character = kFontDefaultChar;
  return kBitmapGlyphs[character - kFontFirstChar][row];
}

constexpr uint64_t BitmapTextWidth(uint64_t length) {
  return length > UINT64_MAX / kFontCellWidth ? UINT64_MAX : static_cast<uint64_t>(length) * kFontCellWidth;
}

// Palette indices, not COLORREF/Win32 BGR: the same 16 VGA colors used by USER.
constexpr uint32_t Palette16Rgb(uint32_t index) {
  constexpr uint32_t colors[16] = {
      0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
      0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
      0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
      0xff5555, 0xff55ff, 0xffff55, 0xffffff};
  return colors[index & 15];
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

constexpr uint32_t Palette16ScreenColor(uint32_t index, uint32_t bitsPerPixel) {
  const auto rgb = Palette16Rgb(index);
  if (bitsPerPixel == 8) return RgbTo332(rgb);
  if (bitsPerPixel == 15) return RgbTo555(rgb);
  if (bitsPerPixel == 16) return RgbTo565(rgb);
  if (bitsPerPixel == 24) return rgb;
  if (bitsPerPixel == 32) return 0xff000000u | rgb;
  return index;
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

// Emits horizontal glyph runs in target coordinates. The caller clips every
// half-open rectangle and supplies an adapter-aware fill (never raw VGA stores).
// Text is a single byte-oriented line; unsupported bytes, including controls,
// render as '?'. Empty text has zero extent, even with an opaque background.
template<typename Fill>
void DrawBitmapText(int64_t x, int64_t y, const char *text, size_t length,
                    uint32_t foreground, uint32_t background, bool opaque, Fill &&fill) {
  if (!text || !length || y > INT64_MAX - kFontCellHeight) return;
  for (size_t i = 0; i < length; ++i) {
    if (x > INT64_MAX - kFontCellWidth) break;
    if (opaque) fill(x, y, x + kFontCellWidth, y + kFontCellHeight, background);
    for (uint32_t row = 0; row < 7; ++row) {
      const auto bits = BitmapGlyphRow(static_cast<uint8_t>(text[i]), row);
      uint32_t col = 0;
      while (col < 5) {
        if (!(bits & (16u >> col))) {
          ++col;
          continue;
        }
        const uint32_t first = col++;
        while (col < 5 && (bits & (16u >> col))) ++col;
        fill(x + first + 1, y + row * 2, x + col + 1, y + row * 2 + 2, foreground);
      }
    }
    x += kFontCellWidth;
  }
}

} // namespace kernel::gfx
