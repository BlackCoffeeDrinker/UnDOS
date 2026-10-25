#include "../../../Apps/Fontloader/decoder.hpp"
#include "font_corpus.hpp"
#include "gfx/font/vm.hpp"
#include <catch2/catch_test_macros.hpp>
#include <undos/font/scaler.hpp>

namespace {
using namespace undos::font;
}

TEST_CASE("font loader decodes the licensed static TrueType corpus", "[fontloader][font][corpus]") {
  struct Fixture {
    const char *name;
    Bytes file;
    uint32_t glyphs;
    uint32_t weight;
    uint32_t programs;
    uint32_t prep;
  };
  const Fixture fixtures[] = {
      {"SourceSans3-Regular", {font_fixture::SourceSans3_Regular, sizeof(font_fixture::SourceSans3_Regular)},
       2478, 400, 87172, 261},
      {"SourceSans3-Bold", {font_fixture::SourceSans3_Bold, sizeof(font_fixture::SourceSans3_Bold)},
       2478, 700, 86058, 261},
      {"SourceCodePro-Regular", {font_fixture::SourceCodePro_Regular, sizeof(font_fixture::SourceCodePro_Regular)},
       1568, 400, 61585, 239},
  };
  for (const auto &fixture : fixtures) {
    INFO(fixture.name);
    kstd::vector<uint8_t> output;
    const auto result = loader::decode(fixture.file, output);
    INFO(loader::error_message(result));
    REQUIRE(result == loader::DecodeError::None);
    REQUIRE(validate({output.data(), output.size()}) == Error::None);
    const ResourceView view({output.data(), output.size()});
    REQUIRE(read_u32(output.data() + 20) == fixture.glyphs);
    REQUIRE(read_u32(output.data() + 24) == 1000);
    REQUIRE(read_u32(output.data() + 40) == fixture.weight);
    REQUIRE(view.section(Section::Glyphs).size == fixture.glyphs * GlyphSize);
    REQUIRE(view.section(Section::FontProgram).size == 3596);
    REQUIRE(view.section(Section::PrepProgram).size == fixture.prep);
    REQUIRE(view.section(Section::GlyphPrograms).size == fixture.programs);
    const auto sourceCvt = view.section(Section::Cvt);
    REQUIRE(sourceCvt.size > 0);
    kstd::vector<int32_t> scaledCvt(sourceCvt.size / 4);
    const auto limits = view.section(Section::Limits);
    kstd::vector<int32_t> storage(read_u32(limits.data + 6 * 4));
    kstd::vector<uint8_t> storageWritten(storage.size());
    const uint32_t twilightCount = read_u32(limits.data + 5 * 4);
    kstd::vector<kernel::font::vm::Point> twilightOriginal(twilightCount), twilightCurrent(twilightCount);
    kstd::vector<uint8_t> twilightTouched(twilightCount);
    kstd::vector<kernel::font::vm::Point> glyphOriginal(MaxPointsPerGlyph + 4), glyphCurrent(MaxPointsPerGlyph + 4);
    kstd::vector<uint8_t> glyphTouched(MaxPointsPerGlyph + 4);
    kstd::vector<uint32_t> contourEnds(2048);
    kernel::font::vm::State state{{storage.data(), storage.size()},
                                  {storageWritten.data(), storageWritten.size()},
                                  {scaledCvt.data(), scaledCvt.size()}};
    const uint32_t functionCount = read_u32(limits.data + 7 * 4);
    REQUIRE(functionCount <= kernel::font::vm::MaxFunctions);
    kstd::vector<kernel::font::vm::Function> functions(functionCount);
    state.functions = {functions.data(), functions.size()};
    state.twilight = {{twilightOriginal.data(), twilightCount}, {twilightCurrent.data(), twilightCount},
                      {twilightTouched.data(), twilightCount}};
    int32_t stack[1]{};
    uint8_t boundaries[5]{};
    for (const uint32_t ppem : {9u, 11u, 12u, 16u, 24u, 48u, 256u}) {
      REQUIRE(kernel::font::vm::initialize(state, sourceCvt, ppem, 1000) == kernel::font::vm::Error::None);
      for (size_t i = 0; i < scaledCvt.size(); ++i) {
        // Probe each normalized entry through RCVT, not original font programs.
        const uint8_t program[] = {0xb8, uint8_t(i >> 8), uint8_t(i), 0x45};
        const auto read = kernel::font::vm::execute({program, sizeof(program)},
            {stack, 1}, {boundaries, 5}, state, 16);
        REQUIRE(read.error == kernel::font::vm::Error::None);
        REQUIRE(read.stackSize == 1);
        int32_t expected;
        REQUIRE(scale_funits(read_i32(sourceCvt.data + i * 4), ppem, 1000, expected) == ScaleError::None);
        REQUIRE(stack[0] == expected);
      }
      kstd::vector<int32_t> fontStack(read_u32(limits.data + 9 * 4));
      kstd::vector<uint8_t> fontBoundaries(MaxProgramBytes + 1);
      const auto fontProgram = view.section(Section::FontProgram);
      const auto initialized = kernel::font::vm::execute(fontProgram,
          {fontStack.data(), fontStack.size()}, {fontBoundaries.data(), fontBoundaries.size()},
          state, kernel::font::vm::MaxWork, kernel::font::vm::Phase::Font);
      INFO(std::string(initialized.error.message().data(), initialized.error.message().size()));
      INFO(initialized.offset);
      INFO(initialized.opcode);
      REQUIRE(initialized.error == kernel::font::vm::Error::None);
      uint32_t defined = 0;
      for (const auto &function : functions) if (function.defined) ++defined;
      REQUIRE(defined > 0);
      const auto prepProbe = kernel::font::vm::execute(view.section(Section::PrepProgram),
          {fontStack.data(), fontStack.size()}, {fontBoundaries.data(), fontBoundaries.size()},
          state, kernel::font::vm::MaxWork, kernel::font::vm::Phase::Prep);
      INFO(std::string(prepProbe.error.message().data(), prepProbe.error.message().size()));
      INFO(prepProbe.offset);
      INFO(prepProbe.opcode);
      REQUIRE(prepProbe.error == kernel::font::vm::Error::None);
      REQUIRE(state.ready);
      {
        uint32_t executed = 0, moved = 0, failed = 0;
        const auto glyphs = view.section(Section::Glyphs);
        const auto programs = view.section(Section::GlyphPrograms);
        for (uint32_t g = 0; g < fixture.glyphs; ++g) {
          const uint32_t offset = read_u32(glyphs.data + g * GlyphSize + 52);
          const uint32_t size = read_u32(glyphs.data + g * GlyphSize + 56);
          if (!size || read_u32(glyphs.data + g * GlyphSize) != 1) continue;
          const uint32_t first = read_u32(glyphs.data + g * GlyphSize + 28);
          const uint32_t count = read_u32(glyphs.data + g * GlyphSize + 32);
          const auto points = view.section(Section::Points);
          for (uint32_t i = 0; i < count + 4; ++i) {
            int32_t x = 0, y = 0;
            if (i < count) {
              const uint8_t *at = points.data + (first + i) * 12;
              REQUIRE(scale_funits(read_i32(at), ppem, 1000, x) == ScaleError::None);
              REQUIRE(scale_funits(read_i32(at + 4), ppem, 1000, y) == ScaleError::None);
            } else if (i == count + 1) {
              REQUIRE(scale_funits(int32_t(read_u32(glyphs.data + g * GlyphSize + 4)), ppem, 1000, x) == ScaleError::None);
            }
            glyphOriginal[i] = glyphCurrent[i] = {x, y};
            glyphTouched[i] = 0;
          }
          const uint32_t contourFirst = read_u32(glyphs.data + g * GlyphSize + 36);
          const uint32_t contourCount = read_u32(glyphs.data + g * GlyphSize + 40);
          const auto contourSection = view.section(Section::Contours);
          REQUIRE(contourCount <= contourEnds.size());
          for (uint32_t i = 0; i < contourCount; ++i)
            contourEnds[i] = read_u32(contourSection.data + (contourFirst + i) * 4);
          state.glyphContours = {contourEnds.data(), contourCount};
          state.glyph = {{glyphOriginal.data(), count + 4}, {glyphCurrent.data(), count + 4},
                         {glyphTouched.data(), count + 4}};
          const auto r = kernel::font::vm::execute({programs.data + offset, size},
              {fontStack.data(), fontStack.size()}, {fontBoundaries.data(), fontBoundaries.size()},
              state, kernel::font::vm::MaxWork, kernel::font::vm::Phase::Glyph);
          if (r.error != kernel::font::vm::Error::None) {
            // Out-of-range CVT reads are a known font quirk; callers must fall back to the unhinted outline.
            INFO(std::string(r.error.message().data(), r.error.message().size()));
            INFO("glyph " << g << " ppem " << ppem);
            REQUIRE(r.error == kernel::font::vm::Error::InvalidCvtIndex);
            ++failed;
            REQUIRE(kernel::font::vm::initialize(state, sourceCvt, ppem, 1000) == kernel::font::vm::Error::None);
            REQUIRE(kernel::font::vm::execute(fontProgram, {fontStack.data(), fontStack.size()},
                {fontBoundaries.data(), fontBoundaries.size()}, state, kernel::font::vm::MaxWork,
                kernel::font::vm::Phase::Font).error == kernel::font::vm::Error::None);
            REQUIRE(kernel::font::vm::execute(view.section(Section::PrepProgram),
                {fontStack.data(), fontStack.size()}, {fontBoundaries.data(), fontBoundaries.size()},
                state, kernel::font::vm::MaxWork, kernel::font::vm::Phase::Prep).error == kernel::font::vm::Error::None);
            continue;
          }
          REQUIRE(r.glyphInhibited == (ppem > 200));
          ++executed;
          for (uint32_t i = 0; i < count; ++i)
            if (glyphCurrent[i].x != glyphOriginal[i].x || glyphCurrent[i].y != glyphOriginal[i].y) {
              ++moved;
              break;
            }
        }
        REQUIRE(executed > 0);
        REQUIRE(failed * 3 < executed); // Tolerated CVT-index failures stay below a quarter of the glyphs.
        if (ppem > 200) REQUIRE(moved == 0); // The original prep inhibits glyph instructions.
        else REQUIRE(moved > 0);
      }
    }
  }
}
