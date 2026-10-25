#include "../../../Apps/Fontloader/decoder.hpp"
#include "font_corpus.hpp"
#include "gfx/font/default_font.hpp"
#include "gfx/font/text.hpp"
#include "user/ui_text.hpp"
#include <catch2/catch_test_macros.hpp>
#include <vector.hpp>

namespace {
using namespace kernel::font;
using undos::font::Bytes;
namespace loader = undos::font::loader;

kstd::vector<uint8_t> decode(const uint8_t *data, size_t size) {
  kstd::vector<uint8_t> output;
  REQUIRE(loader::decode(Bytes{data, size}, output) == loader::DecodeError::None);
  return output;
}

kstd::vector<uint8_t> regular() {
  return decode(font_fixture::SourceSans3_Regular, sizeof(font_fixture::SourceSans3_Regular));
}
kstd::vector<uint8_t> bold() {
  return decode(font_fixture::SourceSans3_Bold, sizeof(font_fixture::SourceSans3_Bold));
}

struct Counted : RefCounted {
  explicit Counted(int &live) : live_(live) { ++live_; }
  ~Counted() { --live_; }
  int &live_;
};
} // namespace

TEST_CASE("intrusive reference releases on last drop", "[font][face]") {
  int live = 0;
  {
    auto a = make_ref<Counted>(live);
    REQUIRE(live == 1);
    auto b = a;
    a.reset();
    REQUIRE(live == 1);
  }
  REQUIRE(live == 0);
}

TEST_CASE("face registry publishes, resolves and closes", "[font][face]") {
  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = registry.publish(regular(), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  REQUIRE(face);
  REQUIRE(face->glyphCount() > 0);
  REQUIRE(registry.close(*handle) == kstd::error_code{});
  REQUIRE_FALSE(registry.acquire(*handle));
  REQUIRE(registry.close(*handle) != kstd::error_code{});
  REQUIRE(face->glyphCount() > 0);
  face.reset();
  REQUIRE(registry.bytes() == 0);

  kstd::vector<uint8_t> junk;
  junk.resize(64);
  REQUIRE_FALSE(registry.publish(static_cast<kstd::vector<uint8_t> &&>(junk), validation).has_value());
}

TEST_CASE("hinted output matches with cache attached and cacheless in forward and reverse order", "[font][face]") {
  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = registry.publish(regular(), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  REQUIRE(face);

  GlyphCache cache;
  REQUIRE(cache.valid());
  auto cached = Realization::create(face, 16, HintPolicy::Hinted, &cache);
  auto plain = Realization::create(face, 16, HintPolicy::Hinted, nullptr);
  REQUIRE(cached.has_value());
  REQUIRE(plain.has_value());

  const uint32_t count = face->glyphCount();
  const uint32_t limit = count < 400 ? count : 400;
  for (uint32_t pass = 0; pass < 2; ++pass) {
    for (uint32_t i = 0; i < limit; ++i) {
      const uint32_t id = pass ? limit - 1 - i : i;
      auto a = (*cached)->glyph(id);
      auto b = (*plain)->glyph(id);
      REQUIRE(a.has_value());
      REQUIRE(b.has_value());
      const auto &x = **a;
      const auto &y = **b;
      REQUIRE(x.width == y.width);
      REQUIRE(x.height == y.height);
      REQUIRE(x.left == y.left);
      REQUIRE(x.top == y.top);
      REQUIRE(x.stride == y.stride);
      REQUIRE(x.advance == y.advance);
      REQUIRE(x.hinted == y.hinted);
      REQUIRE(x.fallbacks == y.fallbacks);
      REQUIRE(x.bits.size() == y.bits.size());
      for (size_t k = 0; k < x.bits.size(); ++k) REQUIRE(x.bits[k] == y.bits[k]);
    }
  }
  REQUIRE((*cached)->stats().cacheHits == 0);
  REQUIRE((*cached)->stats().renders == limit * 2);
  const auto cacheStats = cache.stats();
  REQUIRE(cacheStats.hits == 0);
  REQUIRE(cacheStats.misses == 0);
  REQUIRE(cacheStats.entries == 0);
  REQUIRE(cacheStats.bytes == 0);
}

TEST_CASE("glyph cache evicts, purges and pins", "[font][face]") {
  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = registry.publish(regular(), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  GlyphCache small(4096, 8);
  REQUIRE(small.valid());
  auto realization = Realization::create(face, 16, HintPolicy::Unhinted, &small);
  REQUIRE(realization.has_value());
  auto pinned = (*realization)->glyph(40);
  REQUIRE(pinned.has_value());
  const auto pinnedWidth = (*pinned)->width;
  const auto pinnedHeight = (*pinned)->height;
  const auto pinnedStride = (*pinned)->stride;
  const auto pinnedLeft = (*pinned)->left;
  const auto pinnedTop = (*pinned)->top;
  const auto pinnedAdvance = (*pinned)->advance;
  const auto pinnedBits = (*pinned)->bits;
  const auto rendersBeforeHit = (*realization)->stats().renders;
  auto same = (*realization)->glyph(40);
  REQUIRE(same.has_value());
  REQUIRE(same->get() == pinned->get());
  REQUIRE((*realization)->stats().renders == rendersBeforeHit);
  REQUIRE((*realization)->stats().cacheHits == 1);
  for (uint32_t id = 1; id < 200; ++id) REQUIRE((*realization)->glyph(id).has_value());
  const auto stats = small.stats();
  REQUIRE(stats.entries <= 8);
  REQUIRE(stats.bytes <= 4096);
  REQUIRE(stats.evictions > 0);
  REQUIRE((*pinned)->width == pinnedWidth);
  REQUIRE((*pinned)->height == pinnedHeight);
  REQUIRE((*pinned)->stride == pinnedStride);
  REQUIRE((*pinned)->left == pinnedLeft);
  REQUIRE((*pinned)->top == pinnedTop);
  REQUIRE((*pinned)->advance == pinnedAdvance);
  REQUIRE((*pinned)->bits.size() == pinnedBits.size());
  for (size_t i = 0; i < pinnedBits.size(); ++i) REQUIRE((*pinned)->bits[i] == pinnedBits[i]);

  uint32_t secondValidation = 0;
  auto secondHandle = registry.publish(regular(), secondValidation);
  REQUIRE(secondHandle.has_value());
  auto secondFace = registry.acquire(*secondHandle);
  auto secondRealization = Realization::create(secondFace, 16, HintPolicy::Unhinted, &small);
  REQUIRE(secondRealization.has_value());
  auto otherFaceMask = (*secondRealization)->glyph(40);
  REQUIRE(otherFaceMask.has_value());
  REQUIRE(otherFaceMask->get() != pinned->get());
  REQUIRE(small.find((*secondRealization)->key(40)).get() == otherFaceMask->get());
  small.purge((*realization)->key(1).face);
  REQUIRE(small.stats().entries == 1);
  small.clear();
  REQUIRE(small.stats().entries == 0);
  REQUIRE((*pinned)->bits.size() == pinnedBits.size());
  for (size_t i = 0; i < pinnedBits.size(); ++i) REQUIRE((*pinned)->bits[i] == pinnedBits[i]);
  REQUIRE((*otherFaceMask)->width > 0);
}

TEST_CASE("default font publication is atomic", "[font][face]") {
  FaceRegistry registry;
  GlyphCache cache;
  DefaultFont defaults(registry, cache);
  uint32_t validation = 0;
  auto r = registry.publish(regular(), validation);
  auto b = registry.publish(bold(), validation);
  REQUIRE(r.has_value());
  REQUIRE(b.has_value());

  REQUIRE_FALSE(defaults.snapshot());
  REQUIRE(defaults.publish(*r, *b, 0) != kstd::error_code{});
  REQUIRE(defaults.publish(0xffffffffu, *b, 16) != kstd::error_code{});
  REQUIRE_FALSE(defaults.snapshot());

  REQUIRE(defaults.publish(*r, *b, 16) == kstd::error_code{});
  auto first = defaults.snapshot();
  REQUIRE(first);
  REQUIRE(first.generation == 1);

  REQUIRE(defaults.publish(*r, *b, 0) != kstd::error_code{});
  REQUIRE(defaults.publish(*r, 0xffffffffu, 16) != kstd::error_code{});
  REQUIRE(defaults.snapshot().generation == 1);
  REQUIRE(defaults.publish(*b, *r, 12) == kstd::error_code{});
  REQUIRE(defaults.snapshot().generation == 2);
  REQUIRE(defaults.snapshot().ppem == 12);
  REQUIRE(first.ppem == 16);

  REQUIRE(registry.close(*r) == kstd::error_code{});
  REQUIRE(registry.close(*b) == kstd::error_code{});
  auto kept = defaults.snapshot();
  REQUIRE(kept);
  REQUIRE(kept.regular->glyph(40).has_value());
}

TEST_CASE("text layout maps UTF-8 through the cmap and sums advances", "[font][face]") {
  FaceRegistry registry;
  GlyphCache cache;
  uint32_t validation = 0;
  auto handle = registry.publish(regular(), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  auto realization = Realization::create(face, 16, HintPolicy::Hinted, &cache);
  REQUIRE(realization.has_value());

  REQUIRE(face->glyphFor('A') != 0);
  REQUIRE(face->glyphFor('A') != face->glyphFor('B'));
  REQUIRE(face->glyphFor(0x10ffff) == 0);

  TextRun run;
  REQUIRE(layout_text(**realization, "AB", 2, run) == kstd::error_code{});
  REQUIRE(run.glyphs.size() == 2);
  REQUIRE(run.width == run.glyphs[0]->advance + run.glyphs[1]->advance);
  REQUIRE(run.glyphs[0]->advance > 0);

  // U+00E9 as UTF-8 is one glyph; a lone continuation byte is one replacement glyph.
  REQUIRE(layout_text(**realization, "\xc3\xa9", 2, run) == kstd::error_code{});
  REQUIRE(run.glyphs.size() == 1);
  REQUIRE(layout_text(**realization, "\x80", 1, run) == kstd::error_code{});
  REQUIRE(run.glyphs.size() == 1);
  REQUIRE(layout_text(**realization, "\xc3", 1, run) == kstd::error_code{});
  REQUIRE(run.glyphs.size() == 1);

  REQUIRE(layout_text(**realization, "", 0, run) == kstd::error_code{});
  REQUIRE(run.glyphs.size() == 0);
  REQUIRE(run.width == 0);

  kstd::vector<char> longText;
  for (size_t i = 0; i <= MaxTextBytes; ++i) longText.push_back('A');
  REQUIRE(layout_text(**realization, longText.data(), longText.size(), run) == kernel::KernelError::InvalidSize);
  REQUIRE(run.glyphs.size() == 0);
}

TEST_CASE("shared UTF-8 decoder and bitmap glyph masks preserve the bitmap face", "[font][face]") {
  const uint8_t text[] = {'c', 'a', 'f', 0xc3, 0xa9};
  size_t offset = 0;
  const uint32_t expected[] = {'c', 'a', 'f', 0xe9};
  for (uint32_t codepoint : expected) {
    const auto decoded = decode_utf8(text + offset, sizeof(text) - offset);
    REQUIRE(decoded.codepoint == codepoint);
    offset += decoded.length;
  }
  REQUIRE(offset == sizeof(text));

  const auto bitmap = builtin_bitmap_glyph('I');
  REQUIRE(bitmap);
  REQUIRE((*bitmap)->left == 0);
  REQUIRE((*bitmap)->top == 14);
  REQUIRE((*bitmap)->width == 8);
  REQUIRE((*bitmap)->height == 14);
  REQUIRE((*bitmap)->advance == 8);
  for (uint32_t row = 0; row < 7; ++row) {
    const uint8_t source = kernel::gfx::BitmapGlyphRow('I', row);
    for (uint32_t column = 0; column < 5; ++column) {
      const bool expectedPixel = (source & (16u >> column)) != 0;
      const uint32_t x = column + 1;
      for (uint32_t subrow = 0; subrow < 2; ++subrow) {
        const uint32_t y = row * 2 + subrow;
        const bool actualPixel = ((*bitmap)->bits[y] & (0x80u >> x)) != 0;
        REQUIRE(actualPixel == expectedPixel);
      }
    }
  }
  const auto fallback = builtin_bitmap_glyph(0x1f642);
  const auto question = builtin_bitmap_glyph('?');
  REQUIRE(fallback);
  REQUIRE(question);
  REQUIRE((*fallback)->bits.size() == (*question)->bits.size());
  for (size_t i = 0; i < (*question)->bits.size(); ++i)
    REQUIRE((*fallback)->bits[i] == (*question)->bits[i]);
}

TEST_CASE("chrome text uses pinned masks and falls back to the bitmap font", "[font][face]") {
  using kernel::user::UiText;
  FaceRegistry registry;
  GlyphCache cache;
  DefaultFont defaults(registry, cache);
  uint32_t validation = 0;
  auto r = registry.publish(regular(), validation);
  auto b = registry.publish(bold(), validation);
  REQUIRE(r.has_value());
  REQUIRE(b.has_value());

  const UiText bitmap = UiText::of(defaults.snapshot());
  REQUIRE_FALSE(bitmap.uses_default_font("Hi", 2, false));
  REQUIRE(bitmap.width("Hi", 2, false) == 16);
  REQUIRE(bitmap.fit("Hello", 5, false, 20) == 2);

  REQUIRE(defaults.publish(*r, *b, 12) == kstd::error_code{});
  const UiText ui = UiText::of(defaults.snapshot());
  REQUIRE(ui.uses_default_font("Hello", 5, false));
  REQUIRE(ui.uses_default_font("Hello", 5, true));
  REQUIRE_FALSE(ui.uses_default_font("caf\xc3\xa9", 5, false));
  REQUIRE(ui.width("caf\xc3\xa9", 5, false) == 40);

  const int32_t whole = ui.width("Hello", 5, false);
  REQUIRE(whole > 0);
  REQUIRE(whole != 40);
  REQUIRE(ui.fit("Hello", 5, false, whole) == 5);
  const size_t fitted = ui.fit("Hello", 5, false, whole - 1);
  REQUIRE(fitted < 5);
  REQUIRE(ui.width("Hello", fitted, false) <= whole - 1);

  int inked = 0;
  int32_t minY = 1000, maxY = -1000;
  ui.draw(0, 0, "Hello", 5, false, 7, 1, false, [&](int64_t, int64_t t, int64_t, int64_t bottom, uint32_t color) {
    REQUIRE(color == 7);
    ++inked;
    minY = static_cast<int32_t>(t < minY ? t : minY);
    maxY = static_cast<int32_t>(bottom > maxY ? bottom : maxY);
  });
  REQUIRE(inked > 0);
  REQUIRE(minY >= 0);
  REQUIRE(maxY <= ui.text_height());

  const int32_t previousHeight = ui.text_height();
  REQUIRE(defaults.publish(*r, *b, 40) == kstd::error_code{});
  const UiText larger = UiText::of(defaults.snapshot());
  REQUIRE(larger.uses_default_font("Hello", 5, false));
  REQUIRE(larger.text_height() > previousHeight);
}

TEST_CASE("menu text derives rows from regular mask ink and paints pinned pixels", "[font][menu]") {
  using kernel::user::UiText;
  FaceRegistry registry;
  GlyphCache cache;
  DefaultFont defaults(registry, cache);
  uint32_t validation = 0;
  const auto regularFace = registry.publish(regular(), validation);
  const auto boldFace = registry.publish(bold(), validation);
  REQUIRE(regularFace);
  REQUIRE(boldFace);

  for (const uint32_t ppem : {15u, 24u}) {
    REQUIRE(defaults.publish(*regularFace, *boldFace, ppem) == kstd::error_code{});
    auto font = defaults.snapshot();
    undos::font::PixelLineMetrics metrics{};
    REQUIRE(font.regular->lineMetrics(metrics) == kstd::error_code{});
    REQUIRE(metrics.ascent == (ppem == 15 ? 15u : 24u));
    REQUIRE(metrics.descent == (ppem == 15 ? 5u : 8u));

    uint64_t inkAbove = 0;
    uint64_t inkBelow = 0;
    for (const auto &mask : font.ascii->masks[0]) {
      const int64_t below = static_cast<int64_t>(mask->height) - mask->top;
      if (mask->top > 0 && static_cast<uint64_t>(mask->top) > inkAbove)
        inkAbove = static_cast<uint64_t>(mask->top);
      if (below > 0 && static_cast<uint64_t>(below) > inkBelow)
        inkBelow = static_cast<uint64_t>(below);
    }
    const uint64_t above = font.ascii->ascent[0] > inkAbove ? font.ascii->ascent[0] : inkAbove;
    const uint64_t below = font.ascii->descent[0] > inkBelow ? font.ascii->descent[0] : inkBelow;
    const uint64_t safeHeight = above + below;
    const uint64_t metricHeight = static_cast<uint64_t>(metrics.ascent) +
                                  metrics.descent + metrics.lineGap;
    const uint64_t textHeight = safeHeight > metricHeight ? safeHeight : metricHeight;
    const int32_t expectedRow = static_cast<int32_t>(
        (textHeight > kernel::gfx::kFontCellHeight
             ? textHeight : kernel::gfx::kFontCellHeight) + 4);
    const int32_t minimumRow = ppem == 15 ? 24 : 36;
    REQUIRE(font.ascii->textAbove[0] == static_cast<int32_t>(above));
    REQUIRE(font.ascii->textBelow[0] == static_cast<int32_t>(below));
    const UiText ui = UiText::of(font);
    REQUIRE(ui.row_height() == expectedRow);
    REQUIRE(ui.row_height() >= minimumRow);
    REQUIRE(ui.row_height() == minimumRow);

    constexpr int width = 256;
    constexpr int height = 128;
    constexpr int originX = 12;
    constexpr int boxTop = 7;
    uint8_t actual[width * height]{};
    uint8_t expected[width * height]{};
    const char text[] = "Agjy";
    ui.draw(originX, boxTop, text, sizeof(text) - 1, false, 1, 0, false,
            [&](int64_t left, int64_t top, int64_t right, int64_t bottom, uint32_t color) {
              for (int64_t y = top; y < bottom; ++y)
                for (int64_t x = left; x < right; ++x)
                  if (x >= 0 && x < width && y >= 0 && y < height)
                    actual[y * width + x] = static_cast<uint8_t>(color);
            });
    int32_t pen = originX;
    const int32_t baseline = boxTop + font.ascii->textAbove[0];
    for (const char ch : text) {
      if (!ch) break;
      const auto &mask = *font.ascii->masks[0][static_cast<uint8_t>(ch) - UiGlyphs::FirstCodepoint];
      for (uint32_t row = 0; row < mask.height; ++row) {
        const int32_t y = baseline - mask.top + static_cast<int32_t>(row);
        for (uint32_t col = 0; col < mask.width; ++col) {
          if (mask.bits[row * mask.stride + col / 8] & (0x80u >> (col % 8))) {
            const int32_t x = pen + mask.left + static_cast<int32_t>(col);
            if (x >= 0 && x < width && y >= 0 && y < height) expected[y * width + x] = 1;
          }
        }
      }
      pen += mask.advance;
    }
    for (size_t i = 0; i < sizeof(actual); ++i) REQUIRE(actual[i] == expected[i]);
  }

  UiText bitmap = UiText::of({});
  REQUIRE(bitmap.row_height() == 20);
  REQUIRE(bitmap.text_height() == 16);
  REQUIRE_FALSE(bitmap.uses_default_font("A", 1, false));

  REQUIRE(defaults.publish(*regularFace, *boldFace, 24) == kstd::error_code{});
  const UiText larger = UiText::of(defaults.snapshot());
  constexpr int width = 96;
  constexpr int height = 96;
  uint8_t actual[width * height]{};
  uint8_t expected[width * height]{};
  constexpr int32_t textTop = 8;
  constexpr char fallback[] = "\xc3\xa9";
  REQUIRE_FALSE(larger.uses_default_font(fallback, sizeof(fallback) - 1, false));
  larger.draw(4, textTop, fallback, sizeof(fallback) - 1, false, 1, 0, false,
              [&](int64_t left, int64_t top, int64_t right, int64_t bottom, uint32_t color) {
                for (int64_t y = top; y < bottom; ++y)
                  for (int64_t x = left; x < right; ++x)
                    if (x >= 0 && x < width && y >= 0 && y < height)
                      actual[y * width + x] = static_cast<uint8_t>(color);
              });
  kernel::gfx::DrawBitmapText(4, textTop + (larger.text_height() -
                              static_cast<int32_t>(kernel::gfx::kFontCellHeight)) / 2,
                              fallback, sizeof(fallback) - 1, 1, 0, false,
                              [&](int64_t left, int64_t top, int64_t right, int64_t bottom, uint32_t color) {
                                for (int64_t y = top; y < bottom; ++y)
                                  for (int64_t x = left; x < right; ++x)
                                    if (x >= 0 && x < width && y >= 0 && y < height)
                                      expected[y * width + x] = static_cast<uint8_t>(color);
                              });
  for (size_t i = 0; i < sizeof(actual); ++i) REQUIRE(actual[i] == expected[i]);
}
