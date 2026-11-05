#include "font_storage_test_support.hpp"
#include "fontloader_test_support.hpp"
#include "font_corpus.hpp"
#include "gfx/font/default_font.hpp"
#include "gfx/font/text.hpp"
#include "user/ui_text.hpp"
#include "user/user.hpp"
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector.hpp>

namespace {
using namespace kernel::font;
using undos::font::Bytes;
namespace loader = undos::font::loader;

kstd::vector<uint8_t> decode(const uint8_t *data, size_t size) {
  loader::DecodeError error{};
  auto output = decode_to_kernel(Bytes{data, size}, error);
  REQUIRE(error == loader::DecodeError::None);
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
  auto handle = publish_font_resource(registry, regular(), validation);
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
  REQUIRE_FALSE(publish_font_resource(registry, static_cast<kstd::vector<uint8_t> &&>(junk), validation).has_value());
}

TEST_CASE("hinted realizations render in forward and reverse order", "[font][face]") {
  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = publish_font_resource(registry, regular(), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  REQUIRE(face);

  auto first = Realization::create(face, 16, HintPolicy::Hinted);
  auto second = Realization::create(face, 16, HintPolicy::Hinted);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());

  const uint32_t count = face->glyphCount();
  const uint32_t limit = count < 400 ? count : 400;
  uint32_t expectedRenders = 0;
  for (uint32_t pass = 0; pass < 2; ++pass) {
    for (uint32_t i = 0; i < limit; ++i) {
      const uint32_t id = pass ? limit - 1 - i : i;
      auto a = (*first)->glyph(id);
      auto b = (*second)->glyph(id);
      REQUIRE(a.has_value());
      REQUIRE(b.has_value());
      const auto &x = **a;
      const auto &y = **b;
      if (x.width && x.height) ++expectedRenders;
      REQUIRE((*first)->stats().renders == expectedRenders);
      REQUIRE((*second)->stats().renders == expectedRenders);
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
  REQUIRE((*first)->stats().renders == expectedRenders);
  REQUIRE((*second)->stats().renders == expectedRenders);
}

TEST_CASE("font program runs once per face and cached sizes match fresh faces", "[font][face]") {
  for (const bool useBold : {false, true}) {
    FaceRegistry registry;
    uint32_t validation = 0;
    auto shared = registry.acquire(*publish_font_resource(registry, useBold ? bold() : regular(), validation));
    auto fresh = registry.acquire(*publish_font_resource(registry, useBold ? bold() : regular(), validation));
    REQUIRE(shared);
    REQUIRE(fresh);
    auto small = Realization::create(shared, 12, HintPolicy::Hinted);
    auto cached = Realization::create(shared, 20, HintPolicy::Hinted);
    auto reference = Realization::create(fresh, 20, HintPolicy::Hinted);
    REQUIRE(small);
    REQUIRE(cached);
    REQUIRE(reference);
    REQUIRE(shared->fontProgram().runs() == (shared->fontProgram().per_size() ? 2u : 1u));
    REQUIRE(fresh->fontProgram().runs() == 1);
    REQUIRE((*cached)->hinting() == (*reference)->hinting());
    const uint32_t count = shared->glyphCount();
    for (uint32_t id = 0; id < (count < 300 ? count : 300); ++id) {
      auto a = (*cached)->glyph(id);
      auto b = (*reference)->glyph(id);
      REQUIRE(a.has_value());
      REQUIRE(b.has_value());
      REQUIRE((*a)->width == (*b)->width);
      REQUIRE((*a)->height == (*b)->height);
      REQUIRE((*a)->left == (*b)->left);
      REQUIRE((*a)->top == (*b)->top);
      REQUIRE((*a)->advance == (*b)->advance);
      REQUIRE((*a)->hinted == (*b)->hinted);
      REQUIRE((*a)->bits.size() == (*b)->bits.size());
      for (size_t k = 0; k < (*a)->bits.size(); ++k) REQUIRE((*a)->bits[k] == (*b)->bits[k]);
    }
  }
}

TEST_CASE("unhinted realizations rerender and returned masks stay alive", "[font][face]") {
  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = publish_font_resource(registry, regular(), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  auto realization = Realization::create(face, 16, HintPolicy::Unhinted);
  REQUIRE(realization.has_value());

  auto first = (*realization)->glyph(40);
  REQUIRE(first.has_value());
  const int32_t left = (*first)->left;
  const int32_t top = (*first)->top;
  const uint32_t width = (*first)->width;
  const uint32_t height = (*first)->height;
  const uint32_t stride = (*first)->stride;
  const int32_t advance = (*first)->advance;
  const auto bits = (*first)->bits;
  REQUIRE((*realization)->stats().renders == 1);

  auto second = (*realization)->glyph(40);
  REQUIRE(second.has_value());
  REQUIRE(second->get() != first->get());
  REQUIRE((*realization)->stats().renders == 2);
  REQUIRE((*first)->left == left);
  REQUIRE((*first)->top == top);
  REQUIRE((*first)->width == width);
  REQUIRE((*first)->height == height);
  REQUIRE((*first)->stride == stride);
  REQUIRE((*first)->advance == advance);
  REQUIRE((*first)->bits.size() == bits.size());
  for (size_t i = 0; i < bits.size(); ++i) REQUIRE((*first)->bits[i] == bits[i]);
}

TEST_CASE("default font publication is atomic", "[font][face]") {
  FaceRegistry registry;
  DefaultFont defaults(registry);
  uint32_t validation = 0;
  auto r = publish_font_resource(registry, regular(), validation);
  auto b = publish_font_resource(registry, bold(), validation);
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

TEST_CASE("default metrics match the legacy raster-mask summaries", "[font][face]") {
  FaceRegistry registry;
  DefaultFont defaults(registry);
  uint32_t validation = 0;
  const auto regularFace = publish_font_resource(registry, regular(), validation);
  const auto boldFace = publish_font_resource(registry, bold(), validation);
  REQUIRE(regularFace);
  REQUIRE(boldFace);
  REQUIRE(defaults.publish(*regularFace, *boldFace, 15) == kstd::error_code{});
  const UiFont font = defaults.snapshot();
  const Ref<Face> faces[2] = {registry.acquire(*regularFace), registry.acquire(*boldFace)};

  for (uint32_t style = 0; style < 2; ++style) {
    auto reference = Realization::create(faces[style], 15, HintPolicy::Hinted);
    REQUIRE(reference);
    Ref<GlyphMask> masks[UiGlyphs::Count]{};
    for (uint32_t i = 0; i < UiGlyphs::Count; ++i) {
      auto mask = (*reference)->glyph(font.ascii->glyphIds[style][i]);
      REQUIRE(mask);
      masks[i] = *mask;
      const GlyphMeasurement &measured = font.ascii->measurements[style][i];
      REQUIRE(measured.left == masks[i]->left);
      REQUIRE(measured.top == masks[i]->top);
      REQUIRE(measured.width == masks[i]->width);
      REQUIRE(measured.height == masks[i]->height);
      REQUIRE(measured.advance == masks[i]->advance);
    }
    auto replacement = (*reference)->glyph(font.ascii->replacementGlyphIds[style]);
    REQUIRE(replacement);
    const GlyphMeasurement &replacementMeasurement = font.ascii->replacementMeasurements[style];
    REQUIRE(replacementMeasurement.left == (*replacement)->left);
    REQUIRE(replacementMeasurement.top == (*replacement)->top);
    REQUIRE(replacementMeasurement.width == (*replacement)->width);
    REQUIRE(replacementMeasurement.height == (*replacement)->height);
    REQUIRE(replacementMeasurement.advance == (*replacement)->advance);

    for (uint32_t repertoire = 0; repertoire < 2; ++repertoire) {
      for (uint32_t fallback = 0; fallback < 2; ++fallback) {
        FontMetricsSummary expected{};
        expected.ascent = font.ascii->ascent[style];
        expected.descent = font.ascii->descent[style];
        expected.lineGap = font.ascii->lineGap[style];
        int64_t above = expected.ascent, below = expected.descent;
        int64_t left = INT64_MAX, right = INT64_MIN;
        int64_t top = INT64_MAX, bottom = INT64_MIN;
        uint32_t cellWidth = 0;
        auto include = [&](uint32_t codepoint, const GlyphMask &mask) {
          const bool bitmap = fallback && codepoint >= 0x20 && codepoint != 0x7f &&
                              faces[style]->glyphFor(codepoint) == 0;
          if (bitmap) {
            constexpr int64_t glyphTop = kernel::gfx::kFontAscent;
            constexpr int64_t glyphHeight = kernel::gfx::kFontAscent;
            const int64_t glyphBelow = glyphHeight - glyphTop;
            if (glyphTop > above) above = glyphTop;
            if (glyphBelow > below) below = glyphBelow;
            if (kernel::gfx::kFontCellWidth > cellWidth)
              cellWidth = kernel::gfx::kFontCellWidth;
            for (uint32_t row = 0; row < 7; ++row) {
              const uint8_t bits = kernel::gfx::BitmapGlyphRow(
                  static_cast<uint8_t>(kernel::gfx::kFontDefaultChar), row);
              for (uint32_t column = 0; column < 5; ++column) {
                if (!(bits & (16u >> column))) continue;
                const int64_t glyphLeft = column + 1;
                const int64_t glyphTopInCell = row * 2;
                if (glyphLeft < left) left = glyphLeft;
                if (glyphLeft + 1 > right) right = glyphLeft + 1;
                if (glyphTopInCell < top) top = glyphTopInCell;
                if (glyphTopInCell + 2 > bottom) bottom = glyphTopInCell + 2;
              }
            }
            return;
          }
          const int64_t glyphBelow = static_cast<int64_t>(mask.height) - mask.top;
          if (mask.top > above) above = mask.top;
          if (glyphBelow > below) below = glyphBelow;
          const int64_t inkRight = static_cast<int64_t>(mask.left) + mask.width;
          const int64_t candidateLeft = mask.left < 0 ? mask.left : 0;
          const int64_t candidateRight = inkRight > mask.advance ? inkRight : mask.advance;
          const int64_t candidateWidth = candidateRight - candidateLeft;
          if (candidateWidth > cellWidth) cellWidth = static_cast<uint32_t>(candidateWidth);
          const int64_t inkTop = -static_cast<int64_t>(mask.top);
          const int64_t inkBottom = inkTop + mask.height;
          if (mask.width && mask.height) {
            if (mask.left < left) left = mask.left;
            if (inkRight > right) right = inkRight;
          }
          if (inkTop < top) top = inkTop;
          if (inkBottom > bottom) bottom = inkBottom;
        };
        for (uint32_t i = 0; i < UiGlyphs::Count; ++i)
          include(UiGlyphs::FirstCodepoint + i, *masks[i]);
        if (repertoire) include(0xfffd, **replacement);
        if (left == INT64_MAX) left = right = top = bottom = 0;
        const uint64_t metricHeight = static_cast<uint64_t>(expected.ascent) +
                                      expected.descent + expected.lineGap;
        const uint64_t safeHeight = static_cast<uint64_t>(above + below);
        const uint64_t height = metricHeight > safeHeight ? metricHeight : safeHeight;
        expected.lineHeight = static_cast<uint32_t>(height);
        expected.baselineOffset = static_cast<uint32_t>(above);
        expected.cellWidth = cellWidth;
        expected.cellHeight = static_cast<uint32_t>(height);
        expected.inkLeft = static_cast<int32_t>(left);
        expected.inkTop = static_cast<int32_t>(top);
        expected.inkRight = static_cast<int32_t>(right);
        expected.inkBottom = static_cast<int32_t>(bottom);

        const FontMetricsSummary &actual = font.ascii->summaries[style][repertoire][fallback];
        REQUIRE(actual.ascent == expected.ascent);
        REQUIRE(actual.descent == expected.descent);
        REQUIRE(actual.lineGap == expected.lineGap);
        REQUIRE(actual.lineHeight == expected.lineHeight);
        REQUIRE(actual.baselineOffset == expected.baselineOffset);
        REQUIRE(actual.cellWidth == expected.cellWidth);
        REQUIRE(actual.cellHeight == expected.cellHeight);
        REQUIRE(actual.inkLeft == expected.inkLeft);
        REQUIRE(actual.inkTop == expected.inkTop);
        REQUIRE(actual.inkRight == expected.inkRight);
        REQUIRE(actual.inkBottom == expected.inkBottom);
      }
    }
  }
}

TEST_CASE("default publication measures lazily and text runs reuse glyph IDs", "[font][face]") {
  FaceRegistry registry;
  DefaultFont defaults(registry);
  uint32_t validation = 0;
  const auto regularFace = publish_font_resource(registry, regular(), validation);
  const auto boldFace = publish_font_resource(registry, bold(), validation);
  REQUIRE(regularFace);
  REQUIRE(boldFace);
  REQUIRE(defaults.publish(*regularFace, *boldFace, 15) == kstd::error_code{});

  const UiFont font = defaults.snapshot();
  REQUIRE(font.regular->stats().renders == 0);
  REQUIRE(font.bold->stats().renders == 0);
  for (uint32_t style = 0; style < 2; ++style) {
    for (uint32_t codepoint = UiGlyphs::FirstCodepoint;
         codepoint < UiGlyphs::FirstCodepoint + UiGlyphs::Count; ++codepoint)
      REQUIRE_FALSE(font.ascii->slot_ready(style, codepoint));
    REQUIRE_FALSE(font.ascii->slot_ready(style, 0xfffd));
  }

  for (uint32_t style = 0; style < 2; ++style) {
    uint64_t inkAbove = 0, inkBelow = 0;
    for (const auto &glyph : font.ascii->measurements[style]) {
      const int64_t below = static_cast<int64_t>(glyph.height) - glyph.top;
      if (glyph.top > 0 && static_cast<uint64_t>(glyph.top) > inkAbove)
        inkAbove = static_cast<uint64_t>(glyph.top);
      if (below > 0 && static_cast<uint64_t>(below) > inkBelow)
        inkBelow = static_cast<uint64_t>(below);
    }
    const uint64_t above = font.ascii->ascent[style] > inkAbove
                              ? font.ascii->ascent[style] : inkAbove;
    const uint64_t below = font.ascii->descent[style] > inkBelow
                               ? font.ascii->descent[style] : inkBelow;
    const uint64_t metricHeight = static_cast<uint64_t>(font.ascii->ascent[style]) +
                                  font.ascii->descent[style] + font.ascii->lineGap[style];
    const uint64_t expectedHeight = metricHeight > above + below
                                        ? metricHeight : above + below;
    REQUIRE(font.ascii->textAbove[style] == static_cast<int32_t>(above));
    REQUIRE(font.ascii->textBelow[style] == static_cast<int32_t>(below));
    REQUIRE(font.ascii->textHeight[style] == static_cast<int32_t>(expectedHeight));
  }

  uint8_t actual[64 * 32]{};
  uint8_t expected[64 * 32]{};
  const kernel::user::UiText chrome = kernel::user::UiText::of(font);
  const int32_t bitmapY = (chrome.text_height() -
                           static_cast<int32_t>(kernel::gfx::kFontCellHeight)) / 2;
  {
    kernel::user::Guard sceneLock;
    chrome.draw(0, 0, "Hello", 5, false, 1, 0, false,
                [&](int64_t left, int64_t top, int64_t right, int64_t bottom, uint32_t color) {
                  for (int64_t y = top; y < bottom; ++y)
                    for (int64_t x = left; x < right; ++x)
                      if (x >= 0 && x < 64 && y >= 0 && y < 32)
                        actual[y * 64 + x] = static_cast<uint8_t>(color);
                });
  }
  kernel::gfx::DrawBitmapText(0, bitmapY, "Hello", 5, 1, 0, false,
                              [&](int64_t left, int64_t top, int64_t right,
                                  int64_t bottom, uint32_t color) {
                                for (int64_t y = top; y < bottom; ++y)
                                  for (int64_t x = left; x < right; ++x)
                                    if (x >= 0 && x < 64 && y >= 0 && y < 32)
                                      expected[y * 64 + x] = static_cast<uint8_t>(color);
                              });
  for (size_t i = 0; i < sizeof(actual); ++i) REQUIRE(actual[i] == expected[i]);
  REQUIRE(font.regular->stats().renders == 0);
  REQUIRE_FALSE(font.ascii->slot_ready(0, 'H'));

  TextRun hello;
  REQUIRE(layout_text(*font.regular, "Hello World", 11, hello) == kstd::error_code{});
  REQUIRE(font.regular->stats().renders == 7);
  REQUIRE(hello.glyphs.size() == 11);
  REQUIRE(hello.glyphs[5]->width == 0);
  REQUIRE(hello.glyphs[5]->height == 0);
  REQUIRE(hello.glyphs[5]->advance > 0);

  auto nonAscii = Realization::create(registry.acquire(*regularFace), 15, HintPolicy::Hinted);
  REQUIRE(nonAscii);
  TextRun repeated;
  REQUIRE(layout_text(**nonAscii, "\xc3\xa9\xc3\xa9", 4, repeated) == kstd::error_code{});
  REQUIRE(repeated.glyphs.size() == 2);
  REQUIRE(repeated.glyphs[0].get() == repeated.glyphs[1].get());
  REQUIRE((*nonAscii)->stats().renders == 1);

  TextRun repeatedControls;
  REQUIRE(layout_text(**nonAscii, "\x01\x02", 2, repeatedControls) == kstd::error_code{});
  REQUIRE(repeatedControls.glyphIds.size() == 2);
  REQUIRE(repeatedControls.glyphIds[0] == 0);
  REQUIRE(repeatedControls.glyphIds[1] == 0);
  REQUIRE(repeatedControls.glyphs[0].get() == repeatedControls.glyphs[1].get());
}

TEST_CASE("default glyph slots retry failures and publish concurrent fills once", "[font][face]") {
  FaceRegistry registry;
  DefaultFont defaults(registry);
  uint32_t validation = 0;
  const auto regularFace = publish_font_resource(registry, regular(), validation);
  const auto boldFace = publish_font_resource(registry, bold(), validation);
  REQUIRE(regularFace);
  REQUIRE(boldFace);
  REQUIRE(defaults.publish(*regularFace, *boldFace, 15) == kstd::error_code{});
  const UiFont font = defaults.snapshot();

  constexpr uint32_t style = 0;
  constexpr uint32_t codepoint = 'H';
  const size_t index = codepoint - UiGlyphs::FirstCodepoint;
  const uint32_t originalGlyphId = font.ascii->glyphIds[style][index];
  font.ascii->glyphIds[style][index] = UINT32_MAX;
  const auto failed = font.ascii->glyph(style, codepoint);
  REQUIRE_FALSE(failed);
  REQUIRE(failed.error() == glyph::Error::InvalidGlyph);
  REQUIRE_FALSE(font.ascii->slot_ready(style, codepoint));
  REQUIRE(font.regular->stats().renders == 0);

  font.ascii->glyphIds[style][index] = originalGlyphId;
  constexpr size_t workerCount = 8;
  std::atomic<size_t> ready{0};
  std::atomic<bool> start{false};
  std::array<Ref<GlyphMask>, workerCount> masks{};
  std::array<bool, workerCount> succeeded{};
  std::array<std::thread, workerCount> workers;
  for (size_t i = 0; i < workerCount; ++i) {
    workers[i] = std::thread([&, i] {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
      auto mask = font.ascii->glyph(style, codepoint);
      if (mask) {
        masks[i] = *mask;
        succeeded[i] = true;
      }
    });
  }
  while (ready.load(std::memory_order_acquire) != workerCount)
    std::this_thread::yield();
  start.store(true, std::memory_order_release);
  for (auto &worker : workers) worker.join();

  for (size_t i = 0; i < workerCount; ++i) REQUIRE(succeeded[i]);
  REQUIRE(font.ascii->slot_ready(style, codepoint));
  for (size_t i = 1; i < workerCount; ++i)
    REQUIRE(masks[i].get() == masks[0].get());
  REQUIRE(font.regular->stats().renders == 1);
}

TEST_CASE("text layout maps UTF-8 through the cmap and sums advances", "[font][face]") {
  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = publish_font_resource(registry, regular(), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  auto realization = Realization::create(face, 16, HintPolicy::Hinted);
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
  DefaultFont defaults(registry);
  uint32_t validation = 0;
  auto r = publish_font_resource(registry, regular(), validation);
  auto b = publish_font_resource(registry, bold(), validation);
  REQUIRE(r.has_value());
  REQUIRE(b.has_value());

  const UiText bitmap = UiText::of(defaults.snapshot());
  REQUIRE_FALSE(bitmap.uses_default_font("Hi", 2, false));
  REQUIRE(bitmap.width("Hi", 2, false) == 16);
  REQUIRE(bitmap.fit("Hello", 5, false, 20) == 2);

  REQUIRE(defaults.publish(*r, *b, 12) == kstd::error_code{});
  const UiText ui = UiText::of(defaults.snapshot());
  REQUIRE_FALSE(ui.uses_default_font("Hello", 5, false));
  ui.prewarm("Hello", 5);
  ui.prewarm("Hello", 5, true);
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
  larger.prewarm("Hello", 5);
  REQUIRE(larger.uses_default_font("Hello", 5, false));
  REQUIRE(larger.text_height() > previousHeight);
}

TEST_CASE("menu text derives rows from regular mask ink and paints pinned pixels", "[font][menu]") {
  using kernel::user::UiText;
  FaceRegistry registry;
  DefaultFont defaults(registry);
  uint32_t validation = 0;
  const auto regularFace = publish_font_resource(registry, regular(), validation);
  const auto boldFace = publish_font_resource(registry, bold(), validation);
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
    for (const auto &mask : font.ascii->measurements[0]) {
      const int64_t below = static_cast<int64_t>(mask.height) - mask.top;
      if (mask.top > 0 && static_cast<uint64_t>(mask.top) > inkAbove)
        inkAbove = static_cast<uint64_t>(mask.top);
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
    ui.prewarm("Agjy", 4);
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
      const auto retained = font.ascii->ready_glyph(0, static_cast<uint8_t>(ch));
      REQUIRE(retained);
      const auto &mask = *retained;
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
  larger.prewarm("Hello", 5);
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
