#include "../../../Apps/Fontloader/decoder.hpp"
#include "font_corpus.hpp"
#include "gfx/font/face.hpp"
#include "gfx/font/glyph.hpp"
#include "gfx/font/raster.hpp"
#include "gfx/font/realization.hpp"
#include <catch2/catch_test_macros.hpp>
#include <vector.hpp>

namespace kernel::font {
struct RealizationTestAccess {
  static const vm::State &state(const Realization &realization) { return realization.state_; }
};
} // namespace kernel::font

namespace {
using namespace kernel::font;
using namespace undos::font;

struct Pt {
  int32_t x, y;
  uint32_t on;
};

struct Comp {
  uint32_t glyph, flags;
  int32_t arg1, arg2;
  int32_t matrix[4]{16384, 0, 0, 16384};
};

struct Def {
  uint32_t kind{1};
  uint32_t advance{0};
  int32_t lsb{0};
  int32_t bounds[4]{};
  kstd::vector<Pt> points;
  kstd::vector<uint32_t> ends;
  kstd::vector<Comp> components;
  kstd::vector<uint8_t> program;
};

constexpr uint32_t UnitsPerEm = 64;

void put(kstd::vector<uint8_t> &out, uint32_t value) {
  const size_t at = out.size();
  for (unsigned i = 0; i < 4; ++i) out.push_back(0);
  write_u32(out.data() + at, value);
}

void pad(kstd::vector<uint8_t> &out) {
  while (out.size() & 3) out.push_back(0);
}

// Builds a UFT1 resource with optional stateful programs and VM arrays.
kstd::vector<uint8_t> build(const kstd::vector<Def> &defs,
                            const kstd::vector<uint8_t> &fontProgram = {},
                            const kstd::vector<uint8_t> &prepProgram = {},
                            const kstd::vector<int32_t> &cvt = {},
                            uint32_t storage = 0, uint32_t twilight = 0,
                            uint32_t functions = 0) {
  kstd::vector<uint8_t> sections[SectionCount];
  uint32_t maxPoints = 1, maxContours = 1, maxProgram = 0, maxComponents = 1;
  for (const auto &def : defs) {
    const auto point = static_cast<uint32_t>(sections[1].size() / PointSize);
    const auto contour = static_cast<uint32_t>(sections[2].size() / 4);
    const auto component = static_cast<uint32_t>(sections[3].size() / ComponentSize);
    const auto program = static_cast<uint32_t>(sections[6].size());
    const uint32_t words[] = {def.kind, def.advance, static_cast<uint32_t>(def.lsb),
                              static_cast<uint32_t>(def.bounds[0]), static_cast<uint32_t>(def.bounds[1]),
                              static_cast<uint32_t>(def.bounds[2]), static_cast<uint32_t>(def.bounds[3]),
                              point, static_cast<uint32_t>(def.points.size()), contour,
                              static_cast<uint32_t>(def.ends.size()), component,
                              static_cast<uint32_t>(def.components.size()), program,
                              static_cast<uint32_t>(def.program.size())};
    for (const auto word : words) put(sections[0], word);
    for (const auto &p : def.points) {
      put(sections[1], static_cast<uint32_t>(p.x));
      put(sections[1], static_cast<uint32_t>(p.y));
      put(sections[1], p.on);
    }
    for (const auto end : def.ends) put(sections[2], end);
    for (const auto &c : def.components) {
      put(sections[3], c.glyph);
      put(sections[3], c.flags);
      put(sections[3], static_cast<uint32_t>(c.arg1));
      put(sections[3], static_cast<uint32_t>(c.arg2));
      for (const auto m : c.matrix) put(sections[3], static_cast<uint32_t>(m));
    }
    for (const auto byte : def.program) sections[6].push_back(byte);
    maxPoints = def.points.size() > maxPoints ? static_cast<uint32_t>(def.points.size()) : maxPoints;
    maxContours = def.ends.size() > maxContours ? static_cast<uint32_t>(def.ends.size()) : maxContours;
    maxProgram = def.program.size() > maxProgram ? static_cast<uint32_t>(def.program.size()) : maxProgram;
    maxComponents = def.components.size() > maxComponents ? static_cast<uint32_t>(def.components.size()) : maxComponents;
  }
  put(sections[4], 65);
  put(sections[4], 65);
  put(sections[4], 0);
  for (const auto value : cvt) put(sections[5], static_cast<uint32_t>(value));
  for (const auto byte : fontProgram) sections[7].push_back(byte);
  for (const auto byte : prepProgram) sections[8].push_back(byte);
  if (fontProgram.size() > maxProgram) maxProgram = static_cast<uint32_t>(fontProgram.size());
  if (prepProgram.size() > maxProgram) maxProgram = static_cast<uint32_t>(prepProgram.size());
  const uint32_t limits[] = {maxPoints, maxContours, maxPoints * 4, maxContours * 4, 2, twilight, storage,
                             functions, 0, 16,
                             maxProgram, maxComponents, MaxComponentDepth};
  for (const auto limit : limits) put(sections[9], limit);

  kstd::vector<uint8_t> bytes(PayloadStart, 0);
  uint32_t offsets[SectionCount];
  for (uint32_t i = 0; i < SectionCount; ++i) {
    offsets[i] = static_cast<uint32_t>(bytes.size());
    for (const auto byte : sections[i]) bytes.push_back(byte);
    pad(bytes);
  }
  const uint32_t header[] = {Magic, Version, HeaderSize, static_cast<uint32_t>(bytes.size()), SectionCount,
                             static_cast<uint32_t>(defs.size()), UnitsPerEm, UnitsPerEm,
                             static_cast<uint32_t>(-16), 0, 400, 0};
  for (unsigned i = 0; i < 12; ++i) write_u32(bytes.data() + i * 4, header[i]);
  for (uint32_t i = 0; i < SectionCount; ++i) {
    auto *entry = bytes.data() + HeaderSize + i * DirectoryEntrySize;
    write_u32(entry, i + 1);
    write_u32(entry + 4, offsets[i]);
    write_u32(entry + 8, static_cast<uint32_t>(sections[i].size()));
  }
  return bytes;
}

// Owns the VM state and workspace for a resource, sized from its limits.
struct Rig {
  kstd::vector<uint8_t> bytes;
  ResourceView view;
  kstd::vector<int32_t> cvt, storage, stack;
  kstd::vector<uint8_t> written, boundaries;
  kstd::vector<vm::Function> functions;
  kstd::vector<vm::InstructionDef> instructionDefs;
  kstd::vector<vm::Point> twilightOriginal, twilightCurrent, original, current;
  kstd::vector<uint8_t> twilightTouched, touched, onCurve, onCurveBackup;
  kstd::vector<uint32_t> contourEnds;
  vm::State state{};
  glyph::GlyphLoader *loader{nullptr};

  Rig(kstd::vector<uint8_t> resource, size_t capacity)
      : bytes(static_cast<kstd::vector<uint8_t> &&>(resource)), view({bytes.data(), bytes.size()}) {
    const auto limits = view.section(Section::Limits);
    const auto limit = [&](uint32_t index) { return read_u32(limits.data + index * 4); };
    cvt.resize(view.section(Section::Cvt).size / 4);
    storage.resize(limit(6));
    written.resize(storage.size());
    stack.resize(limit(9) > 256 ? limit(9) : 256);
    boundaries.resize(MaxProgramBytes + 1);
    functions.resize(limit(7));
    instructionDefs.resize(limit(8));
    twilightOriginal.resize(limit(5));
    twilightCurrent.resize(limit(5));
    twilightTouched.resize(limit(5));
    original.resize(capacity);
    current.resize(capacity);
    touched.resize(capacity);
    onCurve.resize(capacity);
    onCurveBackup.resize(capacity);
    contourEnds.resize(capacity);
    state.storage = {storage.data(), storage.size()};
    state.storageWritten = {written.data(), written.size()};
    state.cvt = {cvt.data(), cvt.size()};
    state.functions = {functions.data(), functions.size()};
    state.instructionDefs = {instructionDefs.data(), instructionDefs.size()};
    state.twilight = {{twilightOriginal.data(), limit(5)}, {twilightCurrent.data(), limit(5)},
                      {twilightTouched.data(), limit(5)}};
    loader = new glyph::GlyphLoader(view, state,
        {{original.data(), capacity}, {current.data(), capacity}, {touched.data(), capacity},
         {onCurve.data(), capacity}, {onCurveBackup.data(), capacity}, {contourEnds.data(), capacity},
         {stack.data(), stack.size()}, {boundaries.data(), boundaries.size()}});
  }
  ~Rig() { delete loader; }
  Rig(const Rig &) = delete;
  Rig &operator=(const Rig &) = delete;
};

Def square() {
  Def def;
  def.advance = 7;
  def.bounds[2] = def.bounds[3] = 4;
  def.points = {{0, 0, 1}, {4, 0, 1}, {4, 4, 0}, {0, 4, 1}};
  def.ends = {3};
  return def;
}

Comp component(uint32_t glyphId, uint32_t flags, int32_t arg1, int32_t arg2) {
  Comp value;
  value.glyph = glyphId;
  value.flags = flags;
  value.arg1 = arg1;
  value.arg2 = arg2;
  return value;
}

constexpr uint8_t MoveFirstPointX[] = {0x01, 0xb1, 0, 100, 0x48}; // SVTCA[x], point 0, SCFS 100.
constexpr uint8_t FlipThenFail[] = {0xb0, 0, 0x80, 0x7b};         // FLIPPT, then an undefined opcode.

kstd::vector<uint8_t> program(const uint8_t *data, size_t size) {
  kstd::vector<uint8_t> out;
  for (size_t i = 0; i < size; ++i) out.push_back(data[i]);
  return out;
}

kstd::vector<uint8_t> bytecode(std::initializer_list<uint8_t> bytes) {
  kstd::vector<uint8_t> out;
  for (const auto byte : bytes) out.push_back(byte);
  return out;
}

kstd::vector<uint8_t> stateful_font_program() {
  return bytecode({0xb0, 0, 0x2c, 0xb1, 0, 11, 0x42, 0x2d});
}

kstd::vector<uint8_t> stateful_prep_program() {
  return bytecode({0xb0, 0, 0x2b,             // Call the function defined by fpgm.
                   0xb1, 0, 64, 0x44,         // Initialize CVT[0].
                   0xb0, 0, 0x43,             // Read fpgm's storage write.
                   0xb0, 1, 0x23, 0x42});     // Copy it to storage[1].
}

kstd::vector<uint8_t> state_writer_program() {
  return bytecode({0xb0, 0, 0x43, 0xb0, 1, 0x60, 0xb0, 0, 0x23, 0x42,
                   0xb0, 0, 0x45, 0xb0, 1, 0x60, 0xb0, 0, 0x23, 0x44,
                   0xb0, 0, 0x13, 0xb0, 0, 0xb0, 0, 0x3e});
}

kstd::vector<uint8_t> twilight_reader_program() {
  return bytecode({0xb0, 0, 0x15, 0xb0, 0, 0x46, 0xb0, 2, 0x23, 0x42});
}

bool same_mask(const GlyphMask &a, const GlyphMask &b) {
  if (a.left != b.left || a.top != b.top || a.width != b.width || a.height != b.height ||
      a.stride != b.stride || a.advance != b.advance || a.hinted != b.hinted ||
      a.fallbacks != b.fallbacks || a.bits.size() != b.bits.size())
    return false;
  for (size_t i = 0; i < a.bits.size(); ++i)
    if (a.bits[i] != b.bits[i]) return false;
  return true;
}

bool same_bitmap(const GlyphMask &a, const GlyphMask &b) {
  if (a.left != b.left || a.top != b.top || a.width != b.width || a.height != b.height ||
      a.stride != b.stride || a.advance != b.advance || a.bits.size() != b.bits.size())
    return false;
  for (size_t i = 0; i < a.bits.size(); ++i)
    if (a.bits[i] != b.bits[i]) return false;
  return true;
}
} // namespace

TEST_CASE("glyph loader scales simple glyphs and rounds the advance", "[font][glyph]") {
  Rig rig(build({square()}), 64);
  REQUIRE(validate({rig.bytes.data(), rig.bytes.size()}) == Error::None);
  REQUIRE(rig.loader->prepare(16) == kstd::error_code{}); // 16 units per font unit.
  glyph::Outline outline;
  REQUIRE(rig.loader->load(0, true, outline) == glyph::Error::None);
  REQUIRE(outline.points.size() == 4);
  REQUIRE(outline.points[2].x == 64);
  REQUIRE(outline.points[2].y == 64);
  REQUIRE(outline.onCurve[2] == 0);
  REQUIRE(outline.onCurve[3] == 1);
  REQUIRE(outline.contourEnds.size() == 1);
  REQUIRE(outline.contourEnds[0] == 3);
  REQUIRE(outline.advance == 128); // 7 funits is 112 units, which rounds to 2 pixels.
  REQUIRE_FALSE(outline.hinted);
  REQUIRE(outline.fallbacks == 0);
  REQUIRE(rig.loader->load(1, true, outline) == glyph::Error::InvalidGlyph);
}

TEST_CASE("glyph loader keeps the advance of an absent glyph and translates by the lsb", "[font][glyph]") {
  Def space;
  space.kind = 0;
  space.advance = 12;
  Def shifted = square();
  shifted.lsb = -2;
  shifted.bounds[0] = 2; // The origin phantom point sits at xmin - lsb = 4.
  Rig rig(build({space, shifted}), 64);
  REQUIRE(rig.loader->prepare(64) == kstd::error_code{});
  glyph::Outline outline;
  REQUIRE(rig.loader->load(0, false, outline) == glyph::Error::None);
  REQUIRE(outline.points.size() == 0);
  REQUIRE(outline.advance == 12 * 64);
  REQUIRE(rig.loader->load(1, false, outline) == glyph::Error::None);
  REQUIRE(outline.points[0].x == -4 * 64);
  REQUIRE(outline.points[1].x == 0);
}

TEST_CASE("glyph loader assembles composites with offsets and attachments", "[font][glyph]") {
  Def composite;
  composite.kind = 2;
  composite.advance = 9;
  composite.components = {
      {0, glyph::ArgsAreXyValues, 0, 0, {}},
      {0, glyph::ArgsAreXyValues, 10, -2, {}},
      {0, 0, 2, 0, {}}, // Child point 0 lands on parent point 2.
  };
  Rig rig(build({square(), composite}), 64);
  REQUIRE(rig.loader->prepare(64) == kstd::error_code{});
  glyph::Outline outline;
  REQUIRE(rig.loader->load(1, false, outline) == glyph::Error::None);
  REQUIRE(outline.points.size() == 12);
  REQUIRE(outline.contourEnds.size() == 3);
  REQUIRE(outline.contourEnds[0] == 3);
  REQUIRE(outline.contourEnds[1] == 7);
  REQUIRE(outline.contourEnds[2] == 11);
  REQUIRE(outline.points[4].x == 10 * 64);
  REQUIRE(outline.points[4].y == -2 * 64);
  REQUIRE(outline.points[8].x == outline.points[2].x);
  REQUIRE(outline.points[8].y == outline.points[2].y);
  REQUIRE(outline.advance == 9 * 64);
}

TEST_CASE("glyph loader applies 2x2 transforms and scaled offsets", "[font][glyph]") {
  Def composite;
  composite.kind = 2;
  composite.advance = 9;
  Comp scaled{0, glyph::ArgsAreXyValues | glyph::HaveScale, 0, 0, {8192, 0, 0, 8192}};
  Comp rotated{0, glyph::ArgsAreXyValues | glyph::HaveTwoByTwo, 0, 0, {0, 16384, -16384, 0}};
  composite.components = {scaled, rotated};
  Rig rig(build({square(), composite}), 64);
  REQUIRE(rig.loader->prepare(64) == kstd::error_code{});
  glyph::Outline outline;
  REQUIRE(rig.loader->load(1, false, outline) == glyph::Error::None);
  REQUIRE(outline.points[1].x == 2 * 64); // (4,0) halved.
  REQUIRE(outline.points[2].y == 2 * 64);
  // x' = m0*x + m2*y, y' = m1*x + m3*y: (4,0) -> (0,4) and (0,4) -> (-4,0).
  REQUIRE(outline.points[5].x == 0);
  REQUIRE(outline.points[5].y == 4 * 64);
  REQUIRE(outline.points[7].x == -4 * 64);
  REQUIRE(outline.points[7].y == 0);
}

TEST_CASE("glyph loader rejects bad composites", "[font][glyph]") {
  Def loop;
  loop.kind = 2;
  loop.components = {{1, glyph::ArgsAreXyValues, 0, 0, {}}};
  Rig recursive(build({square(), loop}), 64);
  REQUIRE(recursive.loader->prepare(16) == kstd::error_code{});
  glyph::Outline outline;
  REQUIRE(recursive.loader->load(1, false, outline) == glyph::Error::DepthLimit);

  Def pair;
  pair.kind = 2;
  pair.components = {{0, glyph::ArgsAreXyValues, 0, 0, {}}, {0, glyph::ArgsAreXyValues, 0, 0, {}}};
  Rig small(build({square(), pair}), 9); // Needs 8 points plus 4 phantom points.
  REQUIRE(small.loader->prepare(16) == kstd::error_code{});
  REQUIRE(small.loader->load(1, false, outline) == glyph::Error::PointLimit);

  Def attach;
  attach.kind = 2;
  attach.components = {{0, 0, 9, 0, {}}};
  Rig bad(build({square(), attach}), 64);
  REQUIRE(bad.loader->prepare(16) == kstd::error_code{});
  REQUIRE(bad.loader->load(1, false, outline) == glyph::Error::InvalidComponent);
}

TEST_CASE("glyph loader falls back to unhinted points when a program fails", "[font][glyph]") {
  Def moved = square();
  moved.program = program(MoveFirstPointX, sizeof(MoveFirstPointX));
  Def failing = square();
  failing.program = program(FlipThenFail, sizeof(FlipThenFail));
  Rig rig(build({moved, failing, moved}), 64);
  REQUIRE(rig.loader->prepare(16) == kstd::error_code{});
  REQUIRE(rig.loader->hinting());
  glyph::Outline reference, outline;
  REQUIRE(rig.loader->load(1, false, reference) == glyph::Error::None);
  REQUIRE(reference.points[0].x == 0);
  kstd::vector<vm::Point> plainPoints;
  kstd::vector<uint8_t> plainOnCurve;
  for (const auto point : reference.points) plainPoints.push_back(point);
  for (const auto onCurve : reference.onCurve) plainOnCurve.push_back(onCurve);
  REQUIRE(rig.loader->load(1, true, outline) == glyph::Error::None);
  REQUIRE(outline.fallbacks == 1);
  REQUIRE_FALSE(outline.hinted);
  REQUIRE(outline.points.size() == plainPoints.size());
  for (size_t i = 0; i < plainPoints.size(); ++i) {
    REQUIRE(outline.points[i].x == plainPoints[i].x);
    REQUIRE(outline.points[i].y == plainPoints[i].y);
    REQUIRE(outline.onCurve[i] == plainOnCurve[i]);
  }
  REQUIRE(rig.loader->load(0, true, outline) == glyph::Error::None);
  REQUIRE(outline.hinted);
  REQUIRE(outline.points[0].x == 100);
}

TEST_CASE("realization preserves fpgm prep and reruns hinted state transitions on repeated glyph requests", "[font][glyph][face]") {
  Def writer = square();
  writer.program = state_writer_program();
  Def reader = square();
  reader.program = twilight_reader_program();
  Def secondWriter = writer;
  Def composite;
  composite.kind = 2;
  composite.advance = 9;
  composite.components = {component(0, glyph::ArgsAreXyValues | 0x20, 0, 0),
                          component(1, glyph::ArgsAreXyValues, 8, 0)};

  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = registry.publish(build({writer, reader, secondWriter, composite},
                                       stateful_font_program(), stateful_prep_program(), {0}, 3, 1, 1),
                                 validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  REQUIRE(face);

  auto realization = Realization::create(face, 64, HintPolicy::Hinted);
  auto reference = Realization::create(face, 64, HintPolicy::Hinted);
  REQUIRE(realization.has_value());
  REQUIRE(reference.has_value());
  REQUIRE((*realization)->hinting());
  REQUIRE((*reference)->hinting());

  const auto &initial = RealizationTestAccess::state(**realization);
  REQUIRE(initial.ready);
  REQUIRE(initial.storage.data()[0] == 11); // fpgm function called by prep.
  REQUIRE(initial.storage.data()[1] == 11); // prep read the fpgm write.
  REQUIRE(initial.cvt.data()[0] == 64);
  REQUIRE(initial.twilight.current.data()[0].x == 0);
  REQUIRE(initial.storageWritten.data()[0] == 1);

  const uint32_t requests[] = {0, 1, 2, 0};
  uint32_t writes = 0;
  int32_t lastTwilight = 0;
  int32_t readTwilight = 0;
  for (const uint32_t id : requests) {
    const uint32_t rendersBefore = (*realization)->stats().renders;
    auto actual = (*realization)->glyph(id);
    const auto &comparisonBefore = RealizationTestAccess::state(**reference);
    REQUIRE(comparisonBefore.storage.data()[0] == 11 + static_cast<int32_t>(writes));
    REQUIRE(comparisonBefore.cvt.data()[0] == 64 + static_cast<int32_t>(writes));
    auto expected = (*reference)->glyph(id);
    REQUIRE(actual.has_value());
    REQUIRE(expected.has_value());
    REQUIRE((*realization)->stats().renders == rendersBefore + 1);
    REQUIRE(same_mask(**actual, **expected));
    REQUIRE((*actual)->hinted);
    if (id != 1) {
      ++writes;
      lastTwilight = static_cast<int32_t>(64 + writes);
    } else readTwilight = lastTwilight;
    const auto &realizationState = RealizationTestAccess::state(**realization);
    const auto &comparisonState = RealizationTestAccess::state(**reference);
    const int32_t expectedStorage = 11 + static_cast<int32_t>(writes);
    const int32_t expectedCvt = 64 + static_cast<int32_t>(writes);
    REQUIRE(realizationState.storage.data()[0] == expectedStorage);
    REQUIRE(comparisonState.storage.data()[0] == expectedStorage);
    REQUIRE(realizationState.storage.data()[1] == 11);
    REQUIRE(comparisonState.storage.data()[1] == 11);
    REQUIRE(realizationState.storage.data()[2] == readTwilight);
    REQUIRE(comparisonState.storage.data()[2] == readTwilight);
    REQUIRE(realizationState.cvt.data()[0] == expectedCvt);
    REQUIRE(comparisonState.cvt.data()[0] == expectedCvt);
    REQUIRE(realizationState.twilight.original.data()[0].x == lastTwilight);
    REQUIRE(realizationState.twilight.current.data()[0].x == lastTwilight);
    REQUIRE(comparisonState.twilight.original.data()[0].x == lastTwilight);
    REQUIRE(comparisonState.twilight.current.data()[0].x == lastTwilight);
    REQUIRE(realizationState.twilight.touched.data()[0] == 1);
    REQUIRE(comparisonState.twilight.touched.data()[0] == 1);
  }

  auto rendered = (*realization)->glyph(0);
  REQUIRE(rendered.has_value());
  REQUIRE((*realization)->stats().renders == 5);
  const auto &repeatedState = RealizationTestAccess::state(**realization);
  REQUIRE(repeatedState.storage.data()[0] == 15);
  REQUIRE(repeatedState.cvt.data()[0] == 68);
  REQUIRE(repeatedState.twilight.current.data()[0].x == 68);
  auto invalidGlyph = (*realization)->glyph(4);
  REQUIRE_FALSE(invalidGlyph.has_value());
  REQUIRE(invalidGlyph.error() == glyph::Error::InvalidGlyph);
  REQUIRE((*realization)->stats().renders == 5);

  auto unhinted = Realization::create(face, 64, HintPolicy::Unhinted);
  REQUIRE(unhinted.has_value());
  auto unhintedFirst = (*unhinted)->glyph(0);
  REQUIRE(unhintedFirst.has_value());
  REQUIRE_FALSE((*unhintedFirst)->hinted);
  REQUIRE((*unhinted)->stats().renders == 1);
  auto unhintedSecond = (*unhinted)->glyph(0);
  REQUIRE(unhintedSecond.has_value());
  REQUIRE_FALSE((*unhintedSecond)->hinted);
  REQUIRE(unhintedSecond->get() != unhintedFirst->get());
  REQUIRE((*unhinted)->stats().renders == 2);
  const auto &unhintedState = RealizationTestAccess::state(**unhinted);
  REQUIRE(unhintedState.storage.data()[0] == 11);
  REQUIRE(unhintedState.cvt.data()[0] == 64);
}

TEST_CASE("realization shares state across ordered composite components", "[font][glyph][face]") {
  Def writer = square();
  writer.program = state_writer_program();
  Def reader = square();
  reader.program = twilight_reader_program();
  Def composite;
  composite.kind = 2;
  composite.advance = 9;
  composite.components = {component(0, glyph::ArgsAreXyValues | 0x20, 0, 0),
                          component(1, glyph::ArgsAreXyValues, 8, 0)};

  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = registry.publish(build({writer, reader, composite}, stateful_font_program(),
                                       stateful_prep_program(), {0}, 3, 1, 1), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  auto realization = Realization::create(face, 64, HintPolicy::Hinted);
  REQUIRE(realization.has_value());
  auto mask = (*realization)->glyph(2);
  REQUIRE(mask.has_value());
  REQUIRE((*mask)->hinted);
  REQUIRE((*mask)->fallbacks == 0);

  const auto &state = RealizationTestAccess::state(**realization);
  REQUIRE(state.storage.data()[0] == 12);
  REQUIRE(state.storage.data()[1] == 11);
  REQUIRE(state.storage.data()[2] == 65);
  REQUIRE(state.cvt.data()[0] == 65);
  REQUIRE(state.twilight.current.data()[0].x == 65);
  REQUIRE(state.twilight.touched.data()[0] == 1);
}

TEST_CASE("component failures retain per-program fallback and dirty rebuild boundaries", "[font][glyph][face]") {
  Def writer = square();
  writer.program = state_writer_program();
  for (const auto byte : bytecode({0x01, 0xb1, 0, 100, 0x48})) writer.program.push_back(byte);
  Def failing = square();
  failing.program = bytecode({0x01, 0xb1, 0, 200, 0x48, 0xb0, 0, 0x80, 0x7b});
  Def lastWriter = writer;
  Def composite;
  composite.kind = 2;
  composite.advance = 9;
  composite.components = {component(0, glyph::ArgsAreXyValues | 0x20, 0, 0),
                          component(1, glyph::ArgsAreXyValues | 0x20, 8, 0),
                          component(2, glyph::ArgsAreXyValues, 16, 0)};

  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = registry.publish(build({writer, failing, lastWriter, composite}, stateful_font_program(),
                                       stateful_prep_program(), {0}, 3, 1, 1), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  auto realization = Realization::create(face, 64, HintPolicy::Hinted);
  auto comparison = Realization::create(face, 64, HintPolicy::Hinted);
  REQUIRE(realization.has_value());
  REQUIRE(comparison.has_value());
  auto mask = (*realization)->glyph(3);
  auto comparisonMask = (*comparison)->glyph(3);
  REQUIRE(mask.has_value());
  REQUIRE(comparisonMask.has_value());
  REQUIRE(same_mask(**mask, **comparisonMask));
  REQUIRE((*mask)->hinted);
  REQUIRE((*mask)->fallbacks == 1);
  const auto &state = RealizationTestAccess::state(**realization);
  const auto &comparisonState = RealizationTestAccess::state(**comparison);
  REQUIRE(state.ready);
  REQUIRE(state.storage.data()[0] == 12); // The third component ran after a full rebuild.
  REQUIRE(state.cvt.data()[0] == 65);
  REQUIRE(comparisonState.ready);
  REQUIRE(comparisonState.storage.data()[0] == state.storage.data()[0]);
  REQUIRE(comparisonState.cvt.data()[0] == state.cvt.data()[0]);

  Rig rig(build({writer, failing, lastWriter, composite}, stateful_font_program(),
                stateful_prep_program(), {0}, 3, 1, 1), 64);
  REQUIRE(validate({rig.bytes.data(), rig.bytes.size()}) == Error::None);
  REQUIRE(rig.loader->prepare(64) == kstd::error_code{});
  glyph::Outline outline;
  REQUIRE(rig.loader->load(3, true, outline) == glyph::Error::None);
  REQUIRE(outline.hinted);
  REQUIRE(outline.fallbacks == 1);
  REQUIRE(outline.points.size() == 12);
  REQUIRE(outline.points[0].x == 100);
  REQUIRE(outline.points[4].x == 8 * 64);
  REQUIRE(outline.points[8].x == 16 * 64 + 100);
  for (size_t child = 0; child < 3; ++child) {
    REQUIRE(outline.onCurve[child * 4] == 1);
    REQUIRE(outline.onCurve[child * 4 + 2] == 0);
  }
  REQUIRE(rig.touched[0] == 1);
  REQUIRE(rig.touched[4] == 0);
  REQUIRE(rig.touched[8] == 1);
  REQUIRE(rig.storage[0] == 12);
  REQUIRE(rig.cvt[0] == 65);
}

TEST_CASE("dirty loader waits for an eligible hinted program before rebuilding", "[font][glyph][face]") {
  Def writer = square();
  writer.program = state_writer_program();
  Def failing = writer;
  for (const auto byte : bytecode({0x01, 0xb1, 0, 200, 0x48, 0xb0, 0, 0x80, 0x7b}))
    failing.program.push_back(byte);
  Rig rig(build({failing, square(), writer}, stateful_font_program(),
                stateful_prep_program(), {0}, 3, 1, 1), 64);
  REQUIRE(validate({rig.bytes.data(), rig.bytes.size()}) == Error::None);
  REQUIRE(rig.loader->prepare(64) == kstd::error_code{});
  glyph::Outline outline;
  REQUIRE(rig.loader->load(0, true, outline) == glyph::Error::None);
  REQUIRE_FALSE(outline.hinted);
  REQUIRE(outline.fallbacks == 1);
  REQUIRE(outline.points[0].x == 0);
  REQUIRE(outline.onCurve[0] == 1);
  REQUIRE(rig.touched[0] == 0);
  REQUIRE_FALSE(rig.state.ready);
  REQUIRE(rig.storage[0] == 12);
  REQUIRE(rig.cvt[0] == 65);
  REQUIRE(rig.twilightCurrent[0].x == 65);

  REQUIRE(rig.loader->load(1, true, outline) == glyph::Error::None);
  REQUIRE_FALSE(outline.hinted);
  REQUIRE(outline.fallbacks == 0);
  REQUIRE_FALSE(rig.state.ready);
  REQUIRE(rig.storage[0] == 12);
  REQUIRE(rig.cvt[0] == 65);
  REQUIRE(rig.twilightCurrent[0].x == 65);

  REQUIRE(rig.loader->load(2, false, outline) == glyph::Error::None);
  REQUIRE_FALSE(outline.hinted);
  REQUIRE(outline.fallbacks == 0);
  REQUIRE_FALSE(rig.state.ready);
  REQUIRE(rig.storage[0] == 12);
  REQUIRE(rig.cvt[0] == 65);
  REQUIRE(rig.twilightCurrent[0].x == 65);

  REQUIRE(rig.loader->load(2, true, outline) == glyph::Error::None);
  REQUIRE(outline.hinted);
  REQUIRE(outline.fallbacks == 0);
  REQUIRE(rig.state.ready);
  REQUIRE(rig.storage[0] == 12);
  REQUIRE(rig.cvt[0] == 65);
  REQUIRE(rig.twilightOriginal[0].x == 65);
  REQUIRE(rig.twilightCurrent[0].x == 65);
  REQUIRE(rig.twilightTouched[0] == 1);
}

TEST_CASE("simple glyph failure rebuilds before a later realization program", "[font][glyph][face]") {
  Def writer = square();
  writer.program = state_writer_program();
  Def failing = square();
  failing.program = program(FlipThenFail, sizeof(FlipThenFail));

  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = registry.publish(build({writer, failing, writer}, stateful_font_program(),
                                       stateful_prep_program(), {0}, 3, 1, 1), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  auto realization = Realization::create(face, 64, HintPolicy::Hinted);
  auto comparison = Realization::create(face, 64, HintPolicy::Hinted);
  REQUIRE(realization.has_value());
  REQUIRE(comparison.has_value());

  auto failed = (*realization)->glyph(1);
  auto comparisonFailed = (*comparison)->glyph(1);
  REQUIRE(failed.has_value());
  REQUIRE(comparisonFailed.has_value());
  REQUIRE(same_mask(**failed, **comparisonFailed));
  REQUIRE_FALSE((*failed)->hinted);
  REQUIRE((*failed)->fallbacks == 1);
  REQUIRE_FALSE(RealizationTestAccess::state(**realization).ready);

  auto recovered = (*realization)->glyph(0);
  auto comparisonRecovered = (*comparison)->glyph(0);
  REQUIRE(recovered.has_value());
  REQUIRE(comparisonRecovered.has_value());
  REQUIRE(same_mask(**recovered, **comparisonRecovered));
  REQUIRE((*recovered)->hinted);
  REQUIRE((*recovered)->fallbacks == 0);
  REQUIRE(RealizationTestAccess::state(**realization).ready);
  REQUIRE(RealizationTestAccess::state(**realization).storage.data()[0] == 12);
  REQUIRE(RealizationTestAccess::state(**comparison).storage.data()[0] == 12);
}

TEST_CASE("parent failure restores assembled child output and rebuilds before the next program", "[font][glyph][face]") {
  Def child = square();
  child.program = state_writer_program();
  const auto move = bytecode({0xb1, 0, 100, 0x48});
  for (const auto byte : move) child.program.push_back(byte);
  Def failingParent;
  failingParent.kind = 2;
  failingParent.advance = 9;
  failingParent.components = {component(0, glyph::ArgsAreXyValues | 0x20, 0, 0),
                              component(0, glyph::ArgsAreXyValues | glyph::HaveInstructions, 8, 0)};
  failingParent.program = bytecode({0x01, 0xb1, 0, 200, 0x48, 0xb0, 0, 0x80, 0x7b});
  Def successfulParent = failingParent;
  successfulParent.program.clear();

  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = registry.publish(build({child, failingParent, successfulParent}, stateful_font_program(),
                                       stateful_prep_program(), {0}, 3, 1, 1), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  auto hinted = Realization::create(face, 64, HintPolicy::Hinted);
  auto comparisonHinted = Realization::create(face, 64, HintPolicy::Hinted);
  auto assembled = Realization::create(face, 64, HintPolicy::Hinted);
  REQUIRE(hinted.has_value());
  REQUIRE(comparisonHinted.has_value());
  REQUIRE(assembled.has_value());

  auto parentError = (*hinted)->glyph(1);
  auto comparisonParentError = (*comparisonHinted)->glyph(1);
  auto assembledChildren = (*assembled)->glyph(2);
  REQUIRE(parentError.has_value());
  REQUIRE(comparisonParentError.has_value());
  REQUIRE(assembledChildren.has_value());
  REQUIRE((*parentError)->hinted);
  REQUIRE((*parentError)->fallbacks == 1);
  REQUIRE(same_mask(**parentError, **comparisonParentError));
  REQUIRE(same_bitmap(**parentError, **assembledChildren));
  const auto &failedState = RealizationTestAccess::state(**hinted);
  const auto &comparisonFailedState = RealizationTestAccess::state(**comparisonHinted);
  REQUIRE_FALSE(failedState.ready);
  REQUIRE(failedState.storage.data()[0] == 13);
  REQUIRE_FALSE(comparisonFailedState.ready);
  REQUIRE(comparisonFailedState.storage.data()[0] == failedState.storage.data()[0]);

  auto next = (*hinted)->glyph(0);
  REQUIRE(next.has_value());
  REQUIRE((*next)->hinted);
  const auto &rebuilt = RealizationTestAccess::state(**hinted);
  REQUIRE(rebuilt.ready);
  REQUIRE(rebuilt.storage.data()[0] == 12);
  REQUIRE(rebuilt.cvt.data()[0] == 65);

  Rig rig(build({child, failingParent, successfulParent}, stateful_font_program(),
                stateful_prep_program(), {0}, 3, 1, 1), 64);
  REQUIRE(validate({rig.bytes.data(), rig.bytes.size()}) == Error::None);
  REQUIRE(rig.loader->prepare(64) == kstd::error_code{});
  glyph::Outline outline;
  REQUIRE(rig.loader->load(1, true, outline) == glyph::Error::None);
  REQUIRE(outline.hinted);
  REQUIRE(outline.fallbacks == 1);
  REQUIRE(outline.points.size() == 8);
  REQUIRE(outline.points[0].x == 100);
  REQUIRE(outline.points[4].x == 8 * 64 + 100);
  REQUIRE(outline.onCurve[0] == 1);
  REQUIRE(outline.onCurve[4] == 1);
  for (size_t i = 0; i < outline.points.size(); ++i) REQUIRE(rig.touched[i] == 0);
  REQUIRE_FALSE(rig.state.ready);
  REQUIRE(rig.storage[0] == 13);
}

TEST_CASE("font and prep bytecode errors disable hinting without loader fallback", "[font][glyph][face]") {
  const auto prepWriteThenFail = bytecode({0xb1, 0, 44, 0x42, 0x90});
  Def glyphDef = square();
  glyphDef.program = state_writer_program();
  for (const auto &fontProgram : {bytecode({0xb1, 0, 33, 0x42, 0x90}),
                                  kstd::vector<uint8_t>{}}) {
    FaceRegistry registry;
    uint32_t validation = 0;
    const auto prepProgram = fontProgram.empty() ? prepWriteThenFail : bytecode({0xb1, 1, 77, 0x42});
    auto handle = registry.publish(build({glyphDef}, fontProgram, prepProgram, {}, 3, 1), validation);
    REQUIRE(handle.has_value());
    auto face = registry.acquire(*handle);
    REQUIRE(Realization::create(face, 0, HintPolicy::Hinted).error() == kernel::KernelError::InvalidArgument);
    auto realization = Realization::create(face, 64, HintPolicy::Hinted);
    REQUIRE(realization.has_value());
    REQUIRE_FALSE((*realization)->hinting());
    const auto &state = RealizationTestAccess::state(**realization);
    REQUIRE_FALSE(state.ready);
    if (!fontProgram.empty()) {
      REQUIRE(state.storage.data()[0] == 33);
      REQUIRE(state.storage.data()[1] == 0); // Prep did not run after fpgm failed.
    } else {
      REQUIRE(state.storage.data()[0] == 44); // Prep's partial write is retained.
    }
    auto mask = (*realization)->glyph(0);
    REQUIRE(mask.has_value());
    REQUIRE_FALSE((*mask)->hinted);
    REQUIRE((*mask)->fallbacks == 0);
  }
}

TEST_CASE("raster errors retain completed hinted state without publishing a mask", "[font][glyph][face]") {
  Def large = square();
  large.bounds[2] = large.bounds[3] = 32760;
  large.points = {{0, 0, 1}, {32760, 0, 1}, {32760, 32760, 0}, {0, 32760, 1}};
  large.program = state_writer_program();
  FaceRegistry registry;
  uint32_t validation = 0;
  auto handle = registry.publish(build({large}, stateful_font_program(),
                                       stateful_prep_program(), {0}, 3, 1, 1), validation);
  REQUIRE(handle.has_value());
  auto face = registry.acquire(*handle);
  auto realization = Realization::create(face, 256, HintPolicy::Hinted);
  REQUIRE(realization.has_value());
  auto error = (*realization)->glyph(0);
  REQUIRE_FALSE(error.has_value());
  REQUIRE(error.error().category() == raster::raster_category());
  const auto &afterFailure = RealizationTestAccess::state(**realization);
  REQUIRE(afterFailure.ready);
  REQUIRE(afterFailure.storage.data()[0] == 12);
  REQUIRE(afterFailure.cvt.data()[0] == 65);
  REQUIRE((*realization)->stats().renders == 0);
}

TEST_CASE("glyph loader loads every corpus glyph", "[font][glyph][fontloader]") {
  const Bytes files[] = {
      {font_fixture::SourceSans3_Regular, sizeof(font_fixture::SourceSans3_Regular)},
      {font_fixture::SourceSans3_Bold, sizeof(font_fixture::SourceSans3_Bold)},
      {font_fixture::SourceCodePro_Regular, sizeof(font_fixture::SourceCodePro_Regular)}};
  for (const auto &file : files) {
    kstd::vector<uint8_t> output;
    REQUIRE(loader::decode(file, output) == loader::DecodeError::None);
    Rig rig(static_cast<kstd::vector<uint8_t> &&>(output), MaxPointsPerGlyph + 4);
    const auto glyphs = read_u32(rig.bytes.data() + 20);
    for (const uint32_t ppem : {9u, 16u, 48u, 256u}) {
      REQUIRE(rig.loader->prepare(ppem) == kstd::error_code{});
      uint32_t hinted = 0, fallbacks = 0, composites = 0;
      for (uint32_t id = 0; id < glyphs; ++id) {
        glyph::Outline outline;
        REQUIRE(rig.loader->load(id, true, outline) == glyph::Error::None);
        const auto *record = rig.view.section(Section::Glyphs).data + id * GlyphSize;
        composites += read_u32(record) == 2;
        hinted += outline.hinted;
        fallbacks += outline.fallbacks;
        uint32_t last = 0;
        for (size_t c = 0; c < outline.contourEnds.size(); ++c) {
          REQUIRE((c == 0 || outline.contourEnds[c] > last));
          REQUIRE(outline.contourEnds[c] < outline.points.size());
          last = outline.contourEnds[c];
        }
        REQUIRE(outline.advance % 64 == 0);
      }
      INFO("ppem " << ppem << " hinted " << hinted << " fallbacks " << fallbacks << " hinting " << rig.loader->hinting());
      REQUIRE(composites > 0);
      if (ppem <= 48) REQUIRE(hinted > 0);
      REQUIRE(fallbacks * 4 < glyphs);
    }
  }
}

TEST_CASE("corpus outlines rasterize within bounded scratch", "[font][glyph][raster][fontloader]") {
  const Bytes files[] = {
      {font_fixture::SourceSans3_Regular, sizeof(font_fixture::SourceSans3_Regular)},
      {font_fixture::SourceSans3_Bold, sizeof(font_fixture::SourceSans3_Bold)},
      {font_fixture::SourceCodePro_Regular, sizeof(font_fixture::SourceCodePro_Regular)}};
  for (const auto &file : files) {
    kstd::vector<uint8_t> output;
    REQUIRE(loader::decode(file, output) == loader::DecodeError::None);
    Rig rig(static_cast<kstd::vector<uint8_t> &&>(output), MaxPointsPerGlyph + 4);
    const auto glyphs = read_u32(rig.bytes.data() + 20);
    kstd::vector<raster::Edge> edges(raster::edge_capacity(2048));
    kstd::vector<raster::Crossing> crossings(edges.size());
    kstd::vector<uint8_t> bits(raster::stride_for(raster::MaxBitmapSide) * 512);
    for (const uint32_t ppem : {9u, 16u, 48u}) {
      REQUIRE(rig.loader->prepare(ppem) == kstd::error_code{});
      for (uint32_t id = 0; id < glyphs; ++id) {
        glyph::Outline outline;
        REQUIRE(rig.loader->load(id, true, outline) == glyph::Error::None);
        if (outline.points.size() > 2048) continue;
        raster::Bitmap bitmap;
        const auto error = raster::render(outline, {{edges.data(), edges.size()}, {crossings.data(), crossings.size()}},
                                          {bits.data(), bits.size()}, bitmap);
        INFO("glyph " << id << " ppem " << ppem);
        REQUIRE(error == kstd::error_code{});
        if (!outline.contourEnds.size()) continue;
        uint32_t ink = 0;
        for (size_t i = 0; i < bitmap.bits.size(); ++i) ink += bitmap.bits.data()[i] != 0;
        REQUIRE(ink > 0);
      }
    }
  }
}
