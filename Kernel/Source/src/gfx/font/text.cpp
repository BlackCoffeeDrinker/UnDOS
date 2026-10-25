#include "text.hpp"
#include "../../user/bitmap_font.hpp"

namespace kernel::font {
namespace {
constexpr uint32_t Replacement = 0xfffd;
constexpr uint32_t MaxCodepoint = 0x10ffff;

} // namespace

Utf8Step decode_utf8(const uint8_t *text, size_t size) {
  const uint32_t lead = text[0];
  if (lead < 0x80) return {.codepoint = lead, .length = 1};
  size_t length = 0;
  uint32_t value = 0, minimum = 0;
  if ((lead & 0xe0) == 0xc0) { length = 2; value = lead & 0x1f; minimum = 0x80; }
  else if ((lead & 0xf0) == 0xe0) { length = 3; value = lead & 0x0f; minimum = 0x800; }
  else if ((lead & 0xf8) == 0xf0) { length = 4; value = lead & 0x07; minimum = 0x10000; }
  else return {.codepoint = Replacement, .length = 1};
  if (length > size) return {.codepoint = Replacement, .length = 1};
  for (size_t i = 1; i < length; ++i) {
    if ((text[i] & 0xc0) != 0x80) return {.codepoint = Replacement, .length = 1};
    value = (value << 6) | (text[i] & 0x3fu);
  }
  if (value < minimum || value > MaxCodepoint ||
      (value >= 0xd800 && value <= 0xdfff)) return {.codepoint = Replacement, .length = 1};
  return {.codepoint = value, .length = length};
}

kstd::expected<Ref<GlyphMask>, kstd::error_code> builtin_bitmap_glyph(uint32_t codepoint) {
  const uint8_t character = codepoint >= gfx::kFontFirstChar &&
                                    codepoint <= gfx::kFontLastChar
                                ? static_cast<uint8_t>(codepoint)
                                : static_cast<uint8_t>(gfx::kFontDefaultChar);
  auto mask = make_ref<GlyphMask>();
  if (!mask) return kstd::unexpected(KernelError::OutOfMemory);
  mask->left = 0;
  mask->top = static_cast<int32_t>(gfx::kFontAscent);
  mask->width = gfx::kFontCellWidth;
  mask->height = gfx::kFontAscent;
  mask->stride = 1;
  mask->advance = static_cast<int32_t>(gfx::kFontCellWidth);
  mask->bits.resize(mask->stride * mask->height);
  if (mask->bits.size() != mask->stride * mask->height)
    return kstd::unexpected(KernelError::OutOfMemory);
  for (uint32_t row = 0; row < 7; ++row) {
    const uint8_t bits = gfx::BitmapGlyphRow(character, row);
    for (uint32_t column = 0; column < 5; ++column) {
      if (!(bits & (16u >> column))) continue;
      const uint32_t x = column + 1;
      for (uint32_t y = row * 2; y < row * 2 + 2; ++y)
        mask->bits[y] |= static_cast<uint8_t>(0x80u >> x);
    }
  }
  return mask;
}

kstd::error_code layout_text(Realization &realization, const char *text, size_t size, TextRun &run) {
  run.glyphs = kstd::vector<Ref<GlyphMask>>();
  run.width = 0;
  if (size > MaxTextBytes) return KernelError::InvalidSize;
  if (size && !text) return KernelError::InvalidArgument;
  const auto *bytes = reinterpret_cast<const uint8_t *>(text);
  for (size_t at = 0; at < size;) {
    const auto step = decode_utf8(bytes + at, size - at);
    at += step.length;
    const bool control = step.codepoint < 0x20 || step.codepoint == 0x7f;
    const uint32_t id = control ? 0 : realization.face()->glyphFor(step.codepoint);
    auto mask = realization.glyph(id);
    if (!mask) return mask.error();
    const size_t before = run.glyphs.size();
    run.glyphs.push_back(*mask);
    if (run.glyphs.size() != before + 1) return KernelError::OutOfMemory;
    if ((*mask)->advance > INT32_MAX - run.width) return KernelError::OutOfRange;
    run.width += (*mask)->advance;
  }
  return {};
}

} // namespace kernel::font
