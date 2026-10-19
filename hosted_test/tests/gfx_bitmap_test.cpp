#include <catch2/catch_test_macros.hpp>
#include <kernel/gfx/bitmap_font.hpp>
#include <kernel/gfx/raster_helpers.hpp>
#include "../../drivers/gfx_vga/vga_palette.hpp"

namespace {
struct TextCanvas {
  uint8_t pixels[24 * 20]{};
  size_t calls = 0;
  void draw(int64_t x, int64_t y, const char *text, size_t length, bool opaque) {
    kernel::gfx::DrawBitmapText(x, y, text, length, 15, 7, opaque,
        [&](int64_t left, int64_t top, int64_t right, int64_t bottom, uint32_t color) {
          ++calls;
          if (left < 2) left = 2;
          if (top < 1) top = 1;
          if (right > 22) right = 22;
          if (bottom > 18) bottom = 18;
          for (int64_t row = top; row < bottom; ++row)
            for (int64_t col = left; col < right; ++col) pixels[row * 24 + col] = static_cast<uint8_t>(color);
        });
  }
};
}

TEST_CASE("Original fixed font exposes matching metrics and bounded repertoire", "[gfx][font]") {
  using namespace kernel::gfx;
  STATIC_REQUIRE(kFontCellWidth == 8);
  STATIC_REQUIRE(kFontCellHeight == kFontAscent + kFontDescent);
  STATIC_REQUIRE(BitmapTextWidth(0) == 0);
  STATIC_REQUIRE(BitmapTextWidth(3) == 24);
  for (uint32_t row = 0; row < 7; ++row) {
    REQUIRE(BitmapGlyphRow(0xff, row) == BitmapGlyphRow('?', row));
    REQUIRE(BitmapGlyphRow('\n', row) == BitmapGlyphRow('?', row));
    REQUIRE(BitmapGlyphRow(' ', row) == 0);
  }
  REQUIRE(BitmapGlyphRow('A', 7) == 0);
  REQUIRE(BitmapGlyphRow('A', 0) != BitmapGlyphRow('a', 0));
}

TEST_CASE("Bitmap text emits clipped runs and transparent spacing", "[gfx][font]") {
  TextCanvas empty;
  empty.draw(0, 0, "", 0, true);
  REQUIRE(empty.calls == 0);
  TextCanvas transparent;
  transparent.draw(-2, -2, "AB?", 3, false);
  TextCanvas opaque;
  opaque.draw(-2, -2, "AB?", 3, true);
  bool sawInk = false;
  bool sawBackground = false;
  for (int row = 0; row < 20; ++row) {
    for (int col = 0; col < 24; ++col) {
      const int i = row * 24 + col;
      if (col < 2 || col >= 22 || row < 1 || row >= 18) {
        REQUIRE(opaque.pixels[i] == 0);
        REQUIRE(transparent.pixels[i] == 0);
      }
      if (transparent.pixels[i] == 15) {
        sawInk = true;
        REQUIRE(opaque.pixels[i] == 15);
      }
      if (opaque.pixels[i] == 7) {
        sawBackground = true;
        REQUIRE(transparent.pixels[i] == 0);
      }
    }
  }
  REQUIRE(sawInk);
  REQUIRE(sawBackground);
}

TEST_CASE("Bitmap glyph placement advances exactly one 8x16 cell", "[gfx][font]") {
  TextCanvas pair;
  pair.draw(2, 1, "AA", 2, false);
  for (int y = 1; y < 17; ++y)
    for (int x = 2; x < 10; ++x) REQUIRE(pair.pixels[y * 24 + x] == pair.pixels[y * 24 + x + 8]);
  TextCanvas fallback;
  fallback.draw(2, 1, "\xff", 1, false);
  TextCanvas question;
  question.draw(2, 1, "?", 1, false);
  for (size_t i = 0; i < sizeof(fallback.pixels); ++i) REQUIRE(fallback.pixels[i] == question.pixels[i]);
}

TEST_CASE("Palette16 matches USER colors and packed channel layouts", "[gfx][palette]") {
  using namespace kernel::gfx;
  STATIC_REQUIRE(Palette16Rgb(0) == 0);
  STATIC_REQUIRE(Palette16Rgb(1) == 0x0000aa);
  STATIC_REQUIRE(Palette16Rgb(7) == 0xaaaaaa);
  STATIC_REQUIRE(Palette16Rgb(15) == 0xffffff);
  STATIC_REQUIRE(RgbTo555(0xff0000) == 0x7c00);
  STATIC_REQUIRE(RgbTo565(0xff0000) == 0xf800);
  STATIC_REQUIRE(RgbTo565(0x00ff00) == 0x07e0);
  for (uint32_t i = 0; i < 16; ++i) REQUIRE(RgbToPalette16(Palette16Rgb(i)) == i);
}

TEST_CASE("Blit clipping keeps source and destination aligned", "[gfx][raster]") {
  kernel::gfx::BlitRect r{-2, 1, 3, -1, 10, 9};
  REQUIRE(kernel::gfx::ClipBlit(r, 8, 8, 8, 8));
  REQUIRE(r.dstX == 0);
  REQUIRE(r.dstY == 2);
  REQUIRE(r.srcX == 5);
  REQUIRE(r.srcY == 0);
  REQUIRE(r.width == 3);
  REQUIRE(r.height == 6);
  kernel::gfx::BlitRect outside{INT32_MIN, INT32_MIN, 0, 0, UINT32_MAX, UINT32_MAX};
  REQUIRE_FALSE(kernel::gfx::ClipBlit(outside, 640, 480, 640, 480));
}

TEST_CASE("Clipped line work is bounded by visible dimensions", "[gfx][raster]") {
  size_t points = 0;
  REQUIRE(kernel::gfx::ForEachLinePixel(INT32_MIN, 2, INT32_MAX, 2, 8, 8,
      [&](int32_t x, int32_t y) {
        REQUIRE(x >= 0);
        REQUIRE(x < 8);
        REQUIRE(y == 2);
        ++points;
      }));
  REQUIRE(points == 8);
  points = 0;
  REQUIRE(kernel::gfx::ForEachLinePixel(2, INT32_MAX, 2, INT32_MIN, 8, 8,
      [&](int32_t x, int32_t y) {
        REQUIRE(x == 2);
        REQUIRE(y >= 0);
        REQUIRE(y < 8);
        ++points;
      }));
  REQUIRE(points == 8);
}

TEST_CASE("VGA mode palette overwrites firmware DAC with canonical palette16", "[gfx][palette][vga]") {
  uint8_t channels[48]{};
  size_t writes = 0;
  vga_regs::ProgramPalette16([&](uint16_t port, uint8_t value) {
    if (writes == 0) {
      REQUIRE(port == 0x3c6);
      REQUIRE(value == 0xff);
    } else if (writes == 1) {
      REQUIRE(port == 0x3c8);
      REQUIRE(value == 0);
    } else {
      REQUIRE(port == 0x3c9);
      REQUIRE(value <= 63);
      REQUIRE(writes < 50);
      channels[writes - 2] = value;
    }
    ++writes;
  });
  REQUIRE(writes == 50);
  for (uint32_t index = 0; index < 16; ++index) {
    const auto rgb = kernel::gfx::Palette16Rgb(index);
    REQUIRE(channels[index * 3] == ((rgb >> 18) & 63));
    REQUIRE(channels[index * 3 + 1] == ((rgb >> 10) & 63));
    REQUIRE(channels[index * 3 + 2] == ((rgb >> 2) & 63));
  }
  REQUIRE(channels[45] == 63);
  REQUIRE(channels[46] == 63);
  REQUIRE(channels[47] == 63);
  REQUIRE(channels[18] == 42);// brown, not dark yellow
  REQUIRE(channels[19] == 21);
  REQUIRE(channels[20] == 0);
}
