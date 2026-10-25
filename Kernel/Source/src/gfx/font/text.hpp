#pragma once

#include "realization.hpp"

namespace kernel::font {

// Longest string, in bytes, that one text call accepts.
constexpr size_t MaxTextBytes = 1024;

// A laid-out single line. The glyph masks are pinned by the references held
// here, so the cache may evict them while the line is drawn.
struct TextRun {
  kstd::vector<Ref<GlyphMask>> glyphs;
  // Pen advance of the whole line in pixels.
  int32_t width{0};
};

struct Utf8Step {
  uint32_t codepoint;
  size_t length;
};

Utf8Step decode_utf8(const uint8_t *text, size_t size);
kstd::expected<Ref<GlyphMask>, kstd::error_code> builtin_bitmap_glyph(uint32_t codepoint);

// Decodes UTF-8 (an invalid or truncated sequence becomes U+FFFD) and renders
// or fetches each glyph. The caller must hold no scene, DC or cache lock: a
// cold glyph runs the hinting program and the rasterizer. Controls below
// U+0020 and U+007F draw as .notdef. No kerning, shaping or wrapping.
// Errors: InvalidSize (text longer than MaxTextBytes), OutOfMemory, or the
// error from Realization::glyph.
kstd::error_code layout_text(Realization &realization, const char *text, size_t size, TextRun &run);

} // namespace kernel::font
