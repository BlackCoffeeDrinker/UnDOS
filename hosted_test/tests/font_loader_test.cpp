#include "../../userland/fontloader/decoder.hpp"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace undos::font;
using namespace undos::font::loader;
using Buffer = kstd::vector<uint8_t>;
constexpr uint32_t tag(char a, char b, char c, char d) {
  return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | uint32_t(d);
}
void be16(Buffer &b, size_t at, uint32_t value) {
  b[at] = uint8_t(value >> 8); b[at + 1] = uint8_t(value);
}
void be32(Buffer &b, size_t at, uint32_t value) {
  be16(b, at, value >> 16); be16(b, at + 2, value);
}
uint32_t big32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
struct Table { uint32_t id; Buffer bytes; };
struct Fixture {
  kstd::vector<Table> tables;
  Fixture(bool cmap4 = false) {
    auto &head = add(tag('h','e','a','d'), 54);
    be32(head, 0, 0x10000); be32(head, 12, 0x5f0f3cf5); be16(head, 18, 1024);
    be16(head, 44, 2); be16(head, 50, 1);
    auto &maxp = add(tag('m','a','x','p'), 32);
    be32(maxp, 0, 0x10000); be16(maxp, 4, 3);
    uint16_t limits[] = {3, 1, 6, 2, 2, 8, 16, 4, 4, 32, 8, 2, 4};
    for (size_t i = 0; i < 13; ++i) be16(maxp, 6 + i * 2, limits[i]);
    auto &hhea = add(tag('h','h','e','a'), 36);
    be32(hhea, 0, 0x10000); be16(hhea, 4, 800); be16(hhea, 6, uint16_t(-200));
    be16(hhea, 8, 50); be16(hhea, 34, 2);
    auto &hmtx = add(tag('h','m','t','x'), 10);
    be16(hmtx, 0, 500); be16(hmtx, 4, 600); be16(hmtx, 6, uint16_t(-10));
    be16(hmtx, 8, uint16_t(-12));
    auto &glyf = add(tag('g','l','y','f'), 64);
    // Glyph 0 absent; glyph 1 has three original points and one contour.
    be16(glyf, 0, 1); be16(glyf, 2, uint16_t(-10)); be16(glyf, 4, uint16_t(-20));
    be16(glyf, 6, 20); be16(glyf, 8, 30); be16(glyf, 10, 2);
    be16(glyf, 12, 2); glyf[14] = 0xb0; glyf[15] = 1;
    glyf[16] = 1; glyf[17] = 0; glyf[18] = 1;
    be16(glyf, 19, uint16_t(-10)); be16(glyf, 21, 30); be16(glyf, 23, uint16_t(-20));
    be16(glyf, 25, uint16_t(-20)); be16(glyf, 27, 30); be16(glyf, 29, 20);
    // Glyph 2 retains a signed 2.14 non-diagonal transform and its own program.
    be16(glyf, 32, 0xffff); be16(glyf, 34, uint16_t(-40)); be16(glyf, 36, uint16_t(-40));
    be16(glyf, 38, 40); be16(glyf, 40, 40);
    be16(glyf, 42, 0x183); be16(glyf, 44, 1);
    be16(glyf, 46, uint16_t(-2)); be16(glyf, 48, 3);
    be16(glyf, 50, 0x4000); be16(glyf, 52, 0x2000);
    be16(glyf, 54, 0xe000); be16(glyf, 56, 0x4000);
    be16(glyf, 58, 1); glyf[60] = 0x21;
    auto &loca = add(tag('l','o','c','a'), 16);
    be32(loca, 0, 0); be32(loca, 4, 0); be32(loca, 8, 32); be32(loca, 12, 64);
    if (cmap4) {
      auto &cmap = add(tag('c','m','a','p'), 44);
      be16(cmap, 2, 1); be16(cmap, 4, 3); be16(cmap, 6, 1); be32(cmap, 8, 12);
      be16(cmap, 12, 4); be16(cmap, 14, 32); be16(cmap, 18, 4);
      be16(cmap, 26, 66); be16(cmap, 28, 0xffff);
      be16(cmap, 32, 65); be16(cmap, 34, 0xffff);
      be16(cmap, 36, 0xffc0); be16(cmap, 38, 1);
    } else {
      auto &cmap = add(tag('c','m','a','p'), 40);
      be16(cmap, 2, 1); be16(cmap, 4, 3); be16(cmap, 6, 10); be32(cmap, 8, 12);
      be16(cmap, 12, 12); be32(cmap, 16, 28); be32(cmap, 24, 1);
      be32(cmap, 28, 65); be32(cmap, 32, 66); be32(cmap, 36, 1);
    }
    auto &cvt = add(tag('c','v','t',' '), 4);
    be16(cvt, 0, uint16_t(-100)); be16(cvt, 2, 300);
    auto &fpgm = add(tag('f','p','g','m'), 3);
    fpgm[0] = 0xb0; fpgm[1] = 0; fpgm[2] = 0x2c;
    auto &prep = add(tag('p','r','e','p'), 2); prep[0] = 0xb0; prep[1] = 7;
    auto &os2 = add(tag('O','S','/','2'), 78); be16(os2, 4, 700);
    auto &post = add(tag('p','o','s','t'), 32); be32(post, 0, 0x30000); be32(post, 12, 1);
  }
  Buffer &add(uint32_t id, size_t size) {
    tables.push_back({id, Buffer(size)});
    return tables.back().bytes;
  }
  Buffer &get(uint32_t id) {
    for (auto &t : tables) if (t.id == id) return t.bytes;
    return tables[0].bytes;
  }
  void graph(uint32_t count, bool branching) {
    Buffer source = get(tag('g','l','y','f'));
    auto &glyf = get(tag('g','l','y','f'));
    glyf.resize(32);
    auto &loca = get(tag('l','o','c','a'));
    loca.resize((count + 1) * 4);
    be32(loca, 0, 0); be32(loca, 4, 32);
    for (uint32_t i = 1; i < count; ++i) {
      const uint32_t at = uint32_t(glyf.size()), components = branching ? 2 : 1;
      glyf.resize(at + 10 + components * 8);
      for (uint32_t j = 0; j < 10; ++j) glyf[at + j] = source[32 + j];
      for (uint32_t j = 0; j < components; ++j) {
        be16(glyf, at + 10 + j * 8, j + 1 < components ? 0x23 : 3);
        // After ten binary levels, repeat the same valid 1024-contour subgraph.
        be16(glyf, at + 12 + j * 8, branching && i > 10 ? 10 : i - 1);
      }
      be32(loca, (i + 1) * 4, uint32_t(glyf.size()));
    }
    auto &maxp = get(tag('m','a','x','p'));
    be16(maxp, 4, count); be16(maxp, 10, MaxPointsPerGlyph);
    be16(maxp, 12, MaxContoursPerGlyph); be16(maxp, 30, MaxComponentDepth);
    be16(get(tag('h','h','e','a')), 34, 1);
    get(tag('h','m','t','x')).resize(4 + (count - 1) * 2);
  }
  Buffer build() {
    Buffer result(12 + tables.size() * 16);
    be32(result, 0, 0x10000); be16(result, 4, uint32_t(tables.size()));
    for (size_t i = 0; i < tables.size(); ++i) {
      result.resize(aligned(uint32_t(result.size())));
      const uint32_t offset = uint32_t(result.size()), size = uint32_t(tables[i].bytes.size());
      be32(result, 12 + i * 16, tables[i].id); be32(result, 20 + i * 16, offset);
      be32(result, 24 + i * 16, size);
      for (const auto byte : tables[i].bytes) result.push_back(byte);
    }
    return result;
  }
};
DecodeError run(Fixture &fixture, Buffer &output) {
  const auto file = fixture.build();
  return decode({file.data(), file.size()}, output);
}
} // namespace

TEST_CASE("font loader preserves original coordinates, programs and metrics", "[fontloader]") {
  Fixture fixture;
  Buffer output;
  REQUIRE(run(fixture, output) == DecodeError::None);
  REQUIRE(validate({output.data(), output.size()}) == Error::None);
  ResourceView view({output.data(), output.size()});
  REQUIRE(read_u32(output.data() + 24) == 1024);
  REQUIRE(read_i32(output.data() + 32) == -200);
  REQUIRE(read_u32(output.data() + 40) == 700);
  REQUIRE(read_u32(output.data() + 44) == 3);
  const auto points = view.section(Section::Points);
  REQUIRE(points.size == 36);
  REQUIRE(read_i32(points.data) == -10);
  REQUIRE(read_i32(points.data + 4) == -20);
  REQUIRE(read_i32(points.data + 12) == 20);
  REQUIRE(read_i32(points.data + 16) == 10);
  REQUIRE(read_u32(points.data + 20) == 0);
  REQUIRE(read_i32(points.data + 24) == 0);
  REQUIRE(read_i32(points.data + 28) == 30);
  REQUIRE(read_u32(view.section(Section::Contours).data) == 2);
  const auto components = view.section(Section::Components);
  REQUIRE(components.size == ComponentSize);
  REQUIRE(read_u32(components.data + 4) == 0x183);
  REQUIRE(read_i32(components.data + 8) == -2);
  REQUIRE(read_i32(components.data + 12) == 3);
  REQUIRE(read_i32(components.data + 20) == 8192);
  REQUIRE(read_i32(components.data + 24) == -8192);
  const auto cvt = view.section(Section::Cvt);
  REQUIRE(read_i32(cvt.data) == -100);
  REQUIRE(read_i32(cvt.data + 4) == 300);
  const auto programs = view.section(Section::GlyphPrograms);
  REQUIRE(programs.size == 3);
  REQUIRE(programs.data[0] == 0xb0);
  REQUIRE(programs.data[1] == 1);
  REQUIRE(programs.data[2] == 0x21);
  REQUIRE(view.section(Section::FontProgram).size == 3);
  REQUIRE(view.section(Section::FontProgram).data[2] == 0x2c);
  REQUIRE(view.section(Section::PrepProgram).data[1] == 7);
  const auto glyphs = view.section(Section::Glyphs);
  REQUIRE(read_u32(glyphs.data) == 0);
  REQUIRE(read_u32(glyphs.data + 2 * GlyphSize + 4) == 600);
  REQUIRE(read_i32(glyphs.data + 2 * GlyphSize + 8) == -12);
}

TEST_CASE("font loader decodes cmap formats 4 and 12", "[fontloader]") {
  for (const bool format4 : {false, true}) {
    Fixture fixture(format4);
    Buffer output;
    REQUIRE(run(fixture, output) == DecodeError::None);
    REQUIRE(validate({output.data(), output.size()}) == Error::None);
    const auto cmap = ResourceView({output.data(), output.size()}).section(Section::Cmap);
    REQUIRE(cmap.size == CmapSize);
    REQUIRE(read_u32(cmap.data) == 65);
    REQUIRE(read_u32(cmap.data + 4) == 66);
    REQUIRE(read_u32(cmap.data + 8) == 1);
  }
}

TEST_CASE("font loader rejects truncated SFNT and glyph data", "[fontloader]") {
  Fixture fixture;
  const auto file = fixture.build();
  Buffer output;
  for (size_t length = 0; length < file.size(); ++length) {
    REQUIRE(decode({file.data(), length}, output) != DecodeError::None);
    REQUIRE(output.empty());
  }
  auto &glyf = fixture.get(tag('g','l','y','f'));
  glyf.resize(59);
  REQUIRE(run(fixture, output) != DecodeError::None);
  REQUIRE(output.empty());
}

TEST_CASE("font loader bounds repeat runs and signed coordinate accumulation", "[fontloader]") {
  Buffer output;
  Fixture repeated;
  auto &glyf = repeated.get(tag('g','l','y','f'));
  glyf[16] = 9; glyf[17] = 3;
  REQUIRE(run(repeated, output) == DecodeError::InvalidFlags);
  Fixture coordinates;
  auto &coords = coordinates.get(tag('g','l','y','f'));
  be16(coords, 21, 0x7fff);
  REQUIRE(run(coordinates, output) == DecodeError::InvalidCoordinates);
  REQUIRE(output.empty());
}

TEST_CASE("font loader decodes signed short deltas, repeated flags and short loca", "[fontloader]") {
  Fixture fixture;
  auto &glyf = fixture.get(tag('g','l','y','f'));
  // Three negative x/y short deltas; repeat flag expands to original point flags.
  be16(glyf, 2, uint16_t(-30)); be16(glyf, 4, uint16_t(-30));
  be16(glyf, 6, 0); be16(glyf, 8, 0);
  for (size_t i = 16; i < 32; ++i) glyf[i] = 0;
  glyf[16] = 0x0f; glyf[17] = 2;
  for (size_t i = 18; i < 24; ++i) glyf[i] = 10;
  // Keep loca padding bounded by shortening this glyph.
  Buffer shorter(56);
  for (size_t i = 0; i < 24; ++i) shorter[i] = glyf[i];
  for (size_t i = 0; i < 32; ++i) shorter[i + 24] = glyf[i + 32];
  glyf = kstd::move(shorter);
  auto &loca = fixture.get(tag('l','o','c','a'));
  loca.resize(8);
  be16(loca, 0, 0); be16(loca, 2, 0); be16(loca, 4, 12); be16(loca, 6, 28);
  be16(fixture.get(tag('h','e','a','d')), 50, 0);
  Buffer output;
  REQUIRE(run(fixture, output) == DecodeError::None);
  REQUIRE(validate({output.data(), output.size()}) == Error::None);
  const auto points = ResourceView({output.data(), output.size()}).section(Section::Points);
  for (uint32_t i = 0; i < 3; ++i) {
    REQUIRE(read_i32(points.data + i * PointSize) == -10 * int32_t(i + 1));
    REQUIRE(read_i32(points.data + i * PointSize + 4) == -10 * int32_t(i + 1));
    REQUIRE(read_u32(points.data + i * PointSize + 8) == 1);
  }
}

TEST_CASE("font loader preserves valid point-attached components", "[fontloader]") {
  Fixture fixture;
  auto &glyf = fixture.get(tag('g','l','y','f'));
  glyf.resize(60);
  for (size_t i = 42; i < glyf.size(); ++i) glyf[i] = 0;
  be16(glyf, 42, 0x23); be16(glyf, 44, 1); // First component, XY word args.
  be16(glyf, 50, 0x101); be16(glyf, 52, 1); // Second, point-attached word args.
  be16(glyf, 54, 2); be16(glyf, 56, 1);
  be16(fixture.get(tag('l','o','c','a')), 14, 60);
  Buffer output;
  REQUIRE(run(fixture, output) == DecodeError::None);
  REQUIRE(validate({output.data(), output.size()}) == Error::None);
  const auto components = ResourceView({output.data(), output.size()}).section(Section::Components);
  REQUIRE(components.size == ComponentSize * 2);
  REQUIRE(read_u32(components.data + 4) == 0x23);
  REQUIRE(read_u32(components.data + ComponentSize + 4) == 0x101);
  REQUIRE(read_u32(components.data + ComponentSize + 8) == 2);
  REQUIRE(read_u32(components.data + ComponentSize + 12) == 1);
}

TEST_CASE("font loader checks cmap4 glyph array relative offsets and delta", "[fontloader]") {
  Fixture fixture(true);
  auto &cmap = fixture.get(tag('c','m','a','p'));
  cmap.resize(48);
  be16(cmap, 14, 36); be16(cmap, 36, 1); be16(cmap, 40, 4);
  be16(cmap, 44, 0); be16(cmap, 46, 1); // Zero remains missing; second becomes glyph 2.
  Buffer output;
  REQUIRE(run(fixture, output) == DecodeError::None);
  REQUIRE(validate({output.data(), output.size()}) == Error::None);
  const auto mapping = ResourceView({output.data(), output.size()}).section(Section::Cmap);
  REQUIRE(mapping.size == CmapSize);
  REQUIRE(read_u32(mapping.data) == 66);
  REQUIRE(read_u32(mapping.data + 8) == 2);
}

TEST_CASE("font loader rejects malformed instructions, contours and loca", "[fontloader]") {
  Buffer output;
  Fixture instructions;
  be16(instructions.get(tag('g','l','y','f')), 12, 0xffff);
  REQUIRE(run(instructions, output) == DecodeError::InvalidGlyph);
  Fixture contours;
  be16(contours.get(tag('g','l','y','f')), 10, MaxPointsPerGlyph);
  REQUIRE(run(contours, output) == DecodeError::InvalidGlyph);
  Fixture backwards;
  be32(backwards.get(tag('l','o','c','a')), 8, 65);
  REQUIRE(run(backwards, output) == DecodeError::InvalidLoca);
  Fixture programLimit;
  be16(programLimit.get(tag('m','a','x','p')), 26, 1);
  REQUIRE(run(programLimit, output) == DecodeError::InvalidGlyph);
  Fixture expansion;
  be16(expansion.get(tag('m','a','x','p')), 10, 2);
  REQUIRE(run(expansion, output) == DecodeError::InvalidLimits);
  Fixture padding;
  padding.get(tag('g','l','y','f'))[31] = 1;
  REQUIRE(run(padding, output) == DecodeError::InvalidGlyph);
}

TEST_CASE("font loader rejects bad cmap ranges, offsets and indices", "[fontloader]") {
  Buffer output;
  Fixture index;
  be32(index.get(tag('c','m','a','p')), 36, 3);
  REQUIRE(run(index, output) == DecodeError::InvalidCmap);
  Fixture surrogate;
  auto &cmap = surrogate.get(tag('c','m','a','p'));
  be32(cmap, 28, 0xd800); be32(cmap, 32, 0xd800);
  REQUIRE(run(surrogate, output) == DecodeError::InvalidCmap);
  Fixture offset(true);
  be16(offset.get(tag('c','m','a','p')), 40, 2);
  REQUIRE(run(offset, output) == DecodeError::InvalidCmap);
}

TEST_CASE("font loader rejects composite cycles, flags and invalid attachment", "[fontloader]") {
  Buffer output;
  Fixture cycle;
  be16(cycle.get(tag('g','l','y','f')), 44, 2);
  REQUIRE(run(cycle, output) == DecodeError::CompositeCycle);
  Fixture flags;
  be16(flags.get(tag('g','l','y','f')), 42, 0x18b);
  REQUIRE(run(flags, output) == DecodeError::InvalidFlags);
  Fixture offsets;
  be16(offsets.get(tag('g','l','y','f')), 42, 0x1983);
  REQUIRE(run(offsets, output) == DecodeError::InvalidFlags);
  Fixture attachment;
  be16(attachment.get(tag('g','l','y','f')), 42, 0x181);
  REQUIRE(run(attachment, output) == DecodeError::InvalidComponent);
}

TEST_CASE("font loader enforces global bounds and rejects unsupported outlines", "[fontloader]") {
  Buffer output;
  REQUIRE(decode({nullptr, MaxFileBytes + 1}, output) == DecodeError::InvalidSize);
  Fixture glyphs;
  be16(glyphs.get(tag('m','a','x','p')), 4, MaxGlyphs + 1);
  REQUIRE(run(glyphs, output) == DecodeError::ResourceLimit);
  Fixture depth;
  be16(depth.get(tag('m','a','x','p')), 30, MaxComponentDepth + 1);
  REQUIRE(run(depth, output) == DecodeError::InvalidLimits);
  Fixture cff;
  cff.add(tag('C','F','F',' '), 1);
  REQUIRE(run(cff, output) == DecodeError::UnsupportedFormat);
  Fixture variable;
  variable.add(tag('f','v','a','r'), 1);
  REQUIRE(run(variable, output) == DecodeError::UnsupportedFormat);
}

TEST_CASE("font loader bounds composite depth, expansion and graph work", "[fontloader]") {
  Buffer output;
  Fixture boundary;
  boundary.graph(MaxComponentDepth + 1, false);
  REQUIRE(run(boundary, output) == DecodeError::None);
  REQUIRE(validate({output.data(), output.size()}) == Error::None);
  Fixture depth;
  depth.graph(MaxComponentDepth + 2, false);
  REQUIRE(run(depth, output) == DecodeError::ResourceLimit);
  Fixture expanded;
  expanded.graph(13, true);
  // Change the last component pair to double level 11, exceeding 2048 contours.
  auto &glyf = expanded.get(tag('g','l','y','f'));
  const size_t last = glyf.size() - 26;
  be16(glyf, last + 12, 11); be16(glyf, last + 20, 11);
  REQUIRE(run(expanded, output) == DecodeError::ResourceLimit);
  Fixture work;
  work.graph(300, true);
  REQUIRE(run(work, output) == DecodeError::ResourceLimit);
  REQUIRE(output.empty());
}

TEST_CASE("font loader bounds decoded resource growth from compressed input", "[fontloader]") {
  Fixture fixture;
  constexpr uint32_t count = 44, stride = 142;
  auto &glyf = fixture.get(tag('g','l','y','f'));
  glyf = Buffer(count * stride);
  auto &loca = fixture.get(tag('l','o','c','a'));
  loca = Buffer((count + 1) * 4);
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t at = i * stride;
    be16(glyf, at, 1); be16(glyf, at + 10, MaxPointsPerGlyph - 1);
    for (uint32_t j = 0; j < 64; ++j) {
      glyf[at + 14 + j * 2] = 0x39;
      glyf[at + 15 + j * 2] = 255;
    }
    be32(loca, i * 4, at);
  }
  be32(loca, count * 4, count * stride);
  auto &maxp = fixture.get(tag('m','a','x','p'));
  be16(maxp, 4, count); be16(maxp, 6, MaxPointsPerGlyph);
  be16(fixture.get(tag('h','h','e','a')), 34, 1);
  fixture.get(tag('h','m','t','x')).resize(4 + (count - 1) * 2);
  Buffer output;
  REQUIRE(run(fixture, output) == DecodeError::ResourceLimit);
  REQUIRE(output.empty());
}

TEST_CASE("font loader rejects duplicate and overlapping table records", "[fontloader]") {
  Fixture fixture;
  auto file = fixture.build();
  Buffer output;
  be32(file, 28, big32(file.data() + 12));
  REQUIRE(decode({file.data(), file.size()}, output) == DecodeError::InvalidDirectory);
  file = fixture.build();
  be32(file, 36, big32(file.data() + 20));
  REQUIRE(decode({file.data(), file.size()}, output) == DecodeError::InvalidDirectory);
}

TEST_CASE("font loader successful single-bit mutations retain resource invariants", "[fontloader]") {
  Fixture fixture;
  auto file = fixture.build();
  Buffer output;
  for (size_t i = 0; i < file.size(); ++i) {
    for (uint32_t bit = 0; bit < 8; ++bit) {
      file[i] ^= uint8_t(1u << bit);
      const auto error = decode({file.data(), file.size()}, output);
      if (error == DecodeError::None) {
        REQUIRE(validate({output.data(), output.size()}) == Error::None);
      } else {
        REQUIRE(output.empty());
      }
      file[i] ^= uint8_t(1u << bit);
    }
  }
}
