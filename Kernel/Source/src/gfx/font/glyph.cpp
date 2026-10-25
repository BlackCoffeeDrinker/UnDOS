#include "glyph.hpp"

#include "vm_internal.hpp"

#include <new.hpp>
#include <undos/font/scaler.hpp>

namespace kernel::font::glyph {
namespace {
using undos::font::Section;

// Scaled coordinates stay inside this bound so sums of two never overflow.
constexpr int64_t CoordinateLimit = static_cast<int64_t>(1) << 28;
constexpr int32_t PixelMask = ~(vm::Pixel - 1);

bool fits(int64_t value) { return value >= -CoordinateLimit && value <= CoordinateLimit; }

// Rounds to nearest with ties away from zero, like FreeType's FT_MulFix.
int64_t multiply_unit(int64_t value, int64_t factor) {
  const int64_t product = value * factor;
  const int64_t magnitude = ((product < 0 ? -product : product) + vm::UnitRounding) >> vm::UnitBits;
  return product < 0 ? -magnitude : magnitude;
}

int32_t round_pixel(int32_t value) { return (value + vm::HalfPixel) & PixelMask; }

uint32_t field(const uint8_t *record, uint32_t at) { return undos::font::read_u32(record + at); }
} // namespace

namespace {
class GlyphCategory final : public kstd::error_category {
public:
  [[nodiscard]] const char *name() const noexcept override { return "font_glyph_category"; }
  [[nodiscard]] kstd::string_view message(int value) const noexcept override {
    switch (static_cast<Error>(value)) {
      case Error::None: return "success";
      case Error::InvalidWorkspace: return "glyph loader workspace or resource invalid";
      case Error::InvalidSize: return "pixel size or units per em out of range";
      case Error::InvalidGlyph: return "glyph id or record invalid";
      case Error::InvalidComponent: return "composite component or attachment invalid";
      case Error::DepthLimit: return "composite nesting too deep";
      case Error::PointLimit: return "outline exceeds the workspace";
      case Error::ArithmeticOverflow: return "scaled coordinate out of range";
    }
    return "unknown glyph error";
  }
};
}

const kstd::error_category &glyph_category() noexcept {
  // Error codes keep this reference; never register its destructor.
  alignas(GlyphCategory) static unsigned char storage[sizeof(GlyphCategory)];
  static const GlyphCategory &category = *new (storage) GlyphCategory;
  return category;
}

kstd::error_code GlyphLoader::prepare(uint32_t ppem) {
  ready_ = false;
  hinting_ = false;
  dirty_ = false;
  const auto bytes = view_.bytes();
  const size_t points = ws_.current.size();
  if (bytes.size < undos::font::HeaderSize || !bytes.data || points < PhantomPoints ||
      ws_.original.size() != points || ws_.touched.size() != points ||
      ws_.onCurve.size() != points || ws_.onCurveBackup.size() != points ||
      !ws_.contourEnds.data() || !ws_.stack.data() || !ws_.boundaries.data())
    return Error::InvalidWorkspace;
  glyphCount_ = field(bytes.data, HeaderGlyphCountAt);
  unitsPerEm_ = field(bytes.data, HeaderUnitsPerEmAt);
  ppem_ = ppem;
  int32_t probe;
  if (undos::font::scale_funits(0, ppem_, unitsPerEm_, probe) != undos::font::ScaleError::None)
    return Error::InvalidSize;
  ready_ = true;
  return build_state();
}

kstd::error_code GlyphLoader::build_state() {
  hinting_ = false;
  dirty_ = false;
  if (const auto error = vm::initialize(state_, view_.section(Section::Cvt), ppem_, unitsPerEm_); error)
    return error;
  // A failing font or prep program only disables hinting, never loading.
  const Section programs[] = {Section::FontProgram, Section::PrepProgram};
  for (const auto program : programs) {
    const auto result = vm::execute(view_.section(program), ws_.stack, ws_.boundaries, state_, vm::MaxWork,
                                    program == Section::FontProgram ? vm::Phase::Font : vm::Phase::Prep);
    if (result.error) return Error::None;
  }
  hinting_ = true;
  return Error::None;
}

kstd::error_code GlyphLoader::load(uint32_t glyphId, bool hint, Outline &outline) {
  outline = {};
  if (!ready_) return Error::InvalidWorkspace;
  if (glyphId >= glyphCount_) return Error::InvalidGlyph;
  pointEnd_ = 0;
  contourEnd_ = 0;
  hintedAny_ = false;
  fallbacks_ = 0;
  Part part;
  if (const auto error = load_part(glyphId, 0, hint, part); error) return error;
  // Pen-relative coordinates: the origin phantom point becomes x = 0.
  if (part.origin) {
    for (uint32_t i = 0; i < part.points; ++i) {
      const int64_t x = static_cast<int64_t>(ws_.current.data()[i].x) - part.origin;
      if (!fits(x)) return Error::ArithmeticOverflow;
      ws_.current.data()[i].x = static_cast<int32_t>(x);
    }
  }
  outline.points = {ws_.current.data(), part.points};
  outline.onCurve = {ws_.onCurve.data(), part.points};
  outline.contourEnds = {ws_.contourEnds.data(), part.contours};
  outline.advance = round_pixel(part.edge - part.origin);
  outline.hinted = hintedAny_;
  outline.fallbacks = fallbacks_;
  return Error::None;
}

kstd::error_code GlyphLoader::metrics(const uint8_t *record, int32_t &origin, int32_t &edge) const {
  const int64_t start = static_cast<int64_t>(undos::font::read_i32(record + GlyphXMinAt)) -
                        undos::font::read_i32(record + GlyphLsbAt);
  const int64_t end = start + field(record, GlyphAdvanceAt);
  if (!fits(start) || !fits(end)) return Error::InvalidGlyph;
  if (undos::font::scale_funits(static_cast<int32_t>(start), ppem_, unitsPerEm_, origin) !=
          undos::font::ScaleError::None ||
      undos::font::scale_funits(static_cast<int32_t>(end), ppem_, unitsPerEm_, edge) !=
          undos::font::ScaleError::None)
    return Error::ArithmeticOverflow;
  return Error::None;
}

void GlyphLoader::set_phantoms(uint32_t at, int32_t origin, int32_t edge) {
  const vm::Point phantoms[PhantomPoints] = {{.x = origin, .y = 0}, {.x = edge, .y = 0}, {.x = 0, .y = 0}, {.x = 0, .y = 0}};
  for (uint32_t i = 0; i < PhantomPoints; ++i) {
    ws_.original.data()[at + i] = ws_.current.data()[at + i] = phantoms[i];
    ws_.touched.data()[at + i] = 0;
    ws_.onCurve.data()[at + i] = 1;
  }
}

kstd::error_code GlyphLoader::load_part(uint32_t id, uint32_t depth, bool hint, Part &part) {
  if (depth > undos::font::MaxComponentDepth) return Error::DepthLimit;
  if (id >= glyphCount_) return Error::InvalidGlyph;
  const uint8_t *record = view_.section(Section::Glyphs).data + static_cast<size_t>(id) * undos::font::GlyphSize;
  part = {};
  switch (static_cast<Kind>(field(record, GlyphKindAt))) {
    case Kind::Absent: return metrics(record, part.origin, part.edge);
    case Kind::Simple: return load_simple(record, hint, part);
    case Kind::Composite: return load_composite(record, depth, hint, part);
  }
  return Error::InvalidGlyph;
}

kstd::error_code GlyphLoader::load_simple(const uint8_t *record, bool hint, Part &part) {
  const uint32_t count = field(record, GlyphPointCountAt), first = field(record, GlyphFirstPointAt);
  const uint32_t contours = field(record, GlyphContourCountAt), firstContour = field(record, GlyphFirstContourAt);
  const auto points = view_.section(Section::Points);
  const auto ends = view_.section(Section::Contours);
  if (static_cast<uint64_t>(first) + count > points.size / undos::font::PointSize ||
      static_cast<uint64_t>(firstContour) + contours > ends.size / sizeof(uint32_t))
    return Error::InvalidGlyph;
  if (static_cast<uint64_t>(pointEnd_) + count + PhantomPoints > ws_.current.size() ||
      static_cast<uint64_t>(contourEnd_) + contours > ws_.contourEnds.size())
    return Error::PointLimit;
  if (const auto error = metrics(record, part.origin, part.edge); error) return error;
  const uint32_t base = pointEnd_, contourBase = contourEnd_;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t *at = points.data + (first + i) * undos::font::PointSize;
    vm::Point point;
    if (undos::font::scale_funits(undos::font::read_i32(at), ppem_, unitsPerEm_, point.x) !=
            undos::font::ScaleError::None ||
        undos::font::scale_funits(undos::font::read_i32(at + sizeof(int32_t)), ppem_, unitsPerEm_, point.y) !=
            undos::font::ScaleError::None)
      return Error::ArithmeticOverflow;
    ws_.original.data()[base + i] = ws_.current.data()[base + i] = point;
    ws_.touched.data()[base + i] = 0;
    ws_.onCurve.data()[base + i] = field(at, PointOnCurveAt) & 1;
  }
  for (uint32_t i = 0; i < contours; ++i)
    ws_.contourEnds.data()[contourBase + i] = undos::font::read_u32(ends.data + (firstContour + i) * 4u);
  set_phantoms(base + count, part.origin, part.edge);
  pointEnd_ = base + count;
  contourEnd_ = contourBase + contours;
  part.points = count;
  part.contours = contours;
  run_program(record, base, contourBase, hint, part);
  return Error::None;
}

kstd::error_code GlyphLoader::load_composite(const uint8_t *record, uint32_t depth, bool hint, Part &part) {
  const uint32_t first = field(record, GlyphFirstComponentAt), count = field(record, GlyphComponentCountAt);
  const auto components = view_.section(Section::Components);
  if (static_cast<uint64_t>(first) + count > components.size / undos::font::ComponentSize) return Error::InvalidGlyph;
  if (const auto error = metrics(record, part.origin, part.edge); error) return error;
  const uint32_t base = pointEnd_, contourBase = contourEnd_;
  for (uint32_t i = 0; i < count; ++i) {
    Part child;
    uint32_t flags = 0;
    const uint8_t *component = components.data + (first + i) * undos::font::ComponentSize;
    if (const auto error = add_component(component, base, depth, hint, child, flags); error) return error;
    if (flags & UseMyMetrics) {
      part.origin = child.origin;
      part.edge = child.edge;
    }
  }
  if (static_cast<uint64_t>(pointEnd_) + PhantomPoints > ws_.current.size()) return Error::PointLimit;
  part.points = pointEnd_ - base;
  part.contours = contourEnd_ - contourBase;
  set_phantoms(pointEnd_, part.origin, part.edge);
  run_program(record, base, contourBase, hint, part);
  return Error::None;
}

kstd::error_code GlyphLoader::add_component(const uint8_t *component, uint32_t base, uint32_t depth,
                                            bool hint, Part &child, uint32_t &flags) {
  const uint32_t id = field(component, ComponentGlyphAt);
  flags = field(component, ComponentFlagsAt);
  const int32_t arg1 = undos::font::read_i32(component + ComponentArgAt);
  const int32_t arg2 = undos::font::read_i32(component + ComponentArgAt + sizeof(int32_t));
  int32_t matrix[4];
  for (uint32_t i = 0; i < 4; ++i)
    matrix[i] = undos::font::read_i32(component + ComponentMatrixAt + i * sizeof(int32_t));
  const uint32_t childBase = pointEnd_, childContours = contourEnd_;
  if (const auto error = load_part(id, depth + 1, hint, child); error) return error;
  const uint32_t shift = childBase - base;
  for (uint32_t i = 0; i < child.contours; ++i) ws_.contourEnds.data()[childContours + i] += shift;

  auto *points = ws_.current.data();
  // Matrix words are xscale, scale01, scale10, yscale: x' = m0 x + m2 y; y' = m1 x + m3 y.
  if (flags & HaveTransform) {
    for (uint32_t i = childBase; i < pointEnd_; ++i) {
      const int64_t x = multiply_unit(points[i].x, matrix[0]) + multiply_unit(points[i].y, matrix[2]);
      const int64_t y = multiply_unit(points[i].x, matrix[1]) + multiply_unit(points[i].y, matrix[3]);
      if (!fits(x) || !fits(y)) return Error::ArithmeticOverflow;
      points[i] = {.x = static_cast<int32_t>(x), .y = static_cast<int32_t>(y)};
    }
  }

  int32_t dx, dy;
  if (flags & ArgsAreXyValues) {
    int64_t x = arg1, y = arg2;
    if ((flags & HaveTransform) && (flags & ScaledComponentOffset)) {
      // Mac-style offset scaling by the length of each matrix row, as FreeType does.
      x = multiply_unit(x, vm::detail::isqrt(static_cast<uint64_t>(static_cast<int64_t>(matrix[0]) * matrix[0] + static_cast<int64_t>(matrix[2]) * matrix[2])));
      y = multiply_unit(y, vm::detail::isqrt(static_cast<uint64_t>(static_cast<int64_t>(matrix[3]) * matrix[3] + static_cast<int64_t>(matrix[1]) * matrix[1])));
    }
    if (undos::font::scale_funits(static_cast<int32_t>(x), ppem_, unitsPerEm_, dx) != undos::font::ScaleError::None ||
        undos::font::scale_funits(static_cast<int32_t>(y), ppem_, unitsPerEm_, dy) != undos::font::ScaleError::None)
      return Error::ArithmeticOverflow;
    if ((flags & RoundXyToGrid) && hint && hinting_) {
      dx = round_pixel(dx);
      dy = round_pixel(dy);
    }
  } else {
    // Point attachment: arg1 indexes the outline built so far, arg2 the component.
    if (arg1 < 0 || arg2 < 0 || static_cast<uint32_t>(arg1) >= childBase - base || static_cast<uint32_t>(arg2) >= child.points)
      return Error::InvalidComponent;
    const int64_t x = static_cast<int64_t>(points[base + static_cast<uint32_t>(arg1)].x) - points[childBase + static_cast<uint32_t>(arg2)].x;
    const int64_t y = static_cast<int64_t>(points[base + static_cast<uint32_t>(arg1)].y) - points[childBase + static_cast<uint32_t>(arg2)].y;
    if (!fits(x) || !fits(y)) return Error::ArithmeticOverflow;
    dx = static_cast<int32_t>(x);
    dy = static_cast<int32_t>(y);
  }
  if (dx || dy) {
    for (uint32_t i = childBase; i < pointEnd_; ++i) {
      const int64_t x = static_cast<int64_t>(points[i].x) + dx, y = static_cast<int64_t>(points[i].y) + dy;
      if (!fits(x) || !fits(y)) return Error::ArithmeticOverflow;
      points[i] = {.x = static_cast<int32_t>(x), .y = static_cast<int32_t>(y)};
    }
  }
  return Error::None;
}

void GlyphLoader::run_program(const uint8_t *record, uint32_t base, uint32_t contourBase,
                              bool hint, Part &part) {
  const uint32_t size = field(record, GlyphProgramSizeAt);
  const auto programs = view_.section(Section::GlyphPrograms);
  const uint32_t offset = field(record, GlyphProgramAt);
  if (!hint || !size || static_cast<uint64_t>(offset) + size > programs.size) return;
  if (dirty_) (void)build_state();
  if (!hinting_) return;

  const uint32_t count = part.points + PhantomPoints;
  auto *original = ws_.original.data() + base;
  auto *current = ws_.current.data() + base;
  auto *touched = ws_.touched.data() + base;
  auto *onCurve = ws_.onCurve.data() + base;
  auto *backup = ws_.onCurveBackup.data() + base;
  // A composite program starts from the already assembled, hinted components.
  for (uint32_t i = 0; i < count; ++i) {
    original[i] = current[i];
    touched[i] = 0;
    backup[i] = onCurve[i];
  }
  const uint32_t advance = part.points + PhantomAdvance;
  current[advance].x = round_pixel(current[advance].x);
  state_.glyph = {.original = {original, count}, .current = {current, count}, .touched = {touched, count}, .onCurve = {onCurve, count}};
  state_.glyphContours = {ws_.contourEnds.data() + contourBase, part.contours};
  const auto result = vm::execute({.data = programs.data + offset, .size = size}, ws_.stack, ws_.boundaries, state_,
                                  vm::MaxWork, vm::Phase::Glyph);
  if (result.error || result.glyphInhibited) {
    for (uint32_t i = 0; i < count; ++i) {
      current[i] = original[i];
      touched[i] = 0;
      onCurve[i] = backup[i];
    }
    if (result.error) {
      dirty_ = true;
      ++fallbacks_;
    }
    return;
  }
  hintedAny_ = true;
  part.origin = current[part.points + PhantomOrigin].x;
  part.edge = current[advance].x;
}

} // namespace kernel::font::glyph
