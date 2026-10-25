#pragma once

#include "vm.hpp"

namespace kernel::font::glyph {

// SFNT composite component flags, as retained by the UFT1 Components section.
constexpr uint32_t ArgsAreXyValues = 0x0002;
constexpr uint32_t RoundXyToGrid = 0x0004;
constexpr uint32_t HaveScale = 0x0008;
constexpr uint32_t HaveXyScale = 0x0040;
constexpr uint32_t HaveTwoByTwo = 0x0080;
constexpr uint32_t HaveInstructions = 0x0100;
constexpr uint32_t UseMyMetrics = 0x0200;
constexpr uint32_t ScaledComponentOffset = 0x0800;
constexpr uint32_t HaveTransform = HaveScale | HaveXyScale | HaveTwoByTwo;

// Each outline gets four phantom points after its own points: the origin and
// advance on the x axis, then the unused top and bottom on the y axis.
constexpr uint32_t PhantomPoints = 4;
constexpr uint32_t PhantomOrigin = 0;
constexpr uint32_t PhantomAdvance = 1;

// Glyph record field offsets (see undos/font/resource.hpp) and component words.
constexpr uint32_t GlyphKindAt = 0;
constexpr uint32_t GlyphAdvanceAt = 4;
constexpr uint32_t GlyphLsbAt = 8;
constexpr uint32_t GlyphXMinAt = 12;
constexpr uint32_t GlyphFirstPointAt = 28;
constexpr uint32_t GlyphPointCountAt = 32;
constexpr uint32_t GlyphFirstContourAt = 36;
constexpr uint32_t GlyphContourCountAt = 40;
constexpr uint32_t GlyphFirstComponentAt = 44;
constexpr uint32_t GlyphComponentCountAt = 48;
constexpr uint32_t GlyphProgramAt = 52;
constexpr uint32_t GlyphProgramSizeAt = 56;
constexpr uint32_t PointOnCurveAt = 8;
constexpr uint32_t ComponentGlyphAt = 0;
constexpr uint32_t ComponentFlagsAt = 4;
constexpr uint32_t ComponentArgAt = 8;
constexpr uint32_t ComponentMatrixAt = 16;
constexpr uint32_t HeaderGlyphCountAt = 20;
constexpr uint32_t HeaderUnitsPerEmAt = 24;

enum class Kind : uint32_t { Absent = 0, Simple = 1, Composite = 2 };

// Error codes are values of GlyphCategory. None is zero.
enum class Error : int {
  None = 0, InvalidWorkspace, InvalidSize, InvalidGlyph, InvalidComponent, DepthLimit,
  PointLimit, ArithmeticOverflow
};

const kstd::error_category &glyph_category() noexcept;

inline kstd::error_code make_error_code(Error error) noexcept {
  if (error == Error::None) return {};
  return kstd::error_code(static_cast<int>(error), glyph_category());
}

// Caller-owned buffers, disjoint from each other and from the VM state. The
// five point arrays share one size and bound the outline plus four phantom
// points. The stack and boundaries are VM execution buffers (see vm::execute).
struct Workspace {
  kstd::span<vm::Point> original;
  kstd::span<vm::Point> current;
  kstd::span<uint8_t> touched;
  kstd::span<uint8_t> onCurve;
  kstd::span<uint8_t> onCurveBackup; // Restores FLIPPT effects when a program fails.
  kstd::span<uint32_t> contourEnds;
  kstd::span<int32_t> stack;
  kstd::span<uint8_t> boundaries;
};

// A scaled outline borrowed from the Workspace; valid until the next load().
// Points are F26Dot6 relative to the pen origin. advance is whole pixels.
struct Outline {
  kstd::span<const vm::Point> points{};
  kstd::span<const uint8_t> onCurve{};
  kstd::span<const uint32_t> contourEnds{};
  int32_t advance{0};
  // At least one glyph program ran to completion and was not inhibited.
  bool hinted{false};
  // Count of glyph programs that failed and were replaced by unhinted points.
  uint32_t fallbacks{0};
};

// Builds scaled outlines from a validated UFT1 resource, hinting simple glyphs
// and composites with the original TrueType programs when they succeed. Any
// program failure leaves that glyph or component unhinted and rebuilds the
// shared VM state before the next hinted glyph, so a bad glyph never produces
// a distorted outline or blocks other glyphs. Not thread-safe.
class GlyphLoader {
public:
  // The State must be provisioned from the resource's maxp limits: storage,
  // storageWritten, cvt (one slot per CVT entry), functions, instructionDefs
  // and the twilight zone. The resource and all buffers must outlive the loader.
  GlyphLoader(undos::font::ResourceView view, vm::State &state, const Workspace &workspace)
      : view_(view), state_(state), ws_(workspace) {}

  // Selects the size and runs the font and prep programs. A VM failure in
  // either program is not an error: hinting stays disabled for this size.
  // Errors report an invalid size, workspace or resource.
  kstd::error_code prepare(uint32_t ppem);
  bool hinting() const { return hinting_; }

  // Loads a glyph. Without hint, or when hinting is disabled, the outline is
  // the plain scaled glyph. Composite outlines are assembled from components.
  kstd::error_code load(uint32_t glyphId, bool hint, Outline &outline);

private:
  struct Part {
    uint32_t points{0};
    uint32_t contours{0};
    int32_t origin{0}; // F26Dot6 x of the origin and advance phantom points.
    int32_t edge{0};
  };

  kstd::error_code build_state();
  kstd::error_code load_part(uint32_t id, uint32_t depth, bool hint, Part &part);
  kstd::error_code load_simple(const uint8_t *record, bool hint, Part &part);
  kstd::error_code load_composite(const uint8_t *record, uint32_t depth, bool hint, Part &part);
  kstd::error_code add_component(const uint8_t *component, uint32_t base, uint32_t depth,
                                 bool hint, Part &child, uint32_t &flags);
  kstd::error_code metrics(const uint8_t *record, int32_t &origin, int32_t &edge) const;
  void set_phantoms(uint32_t at, int32_t origin, int32_t edge);
  void run_program(const uint8_t *record, uint32_t base, uint32_t contourBase, bool hint, Part &part);

  undos::font::ResourceView view_;
  vm::State &state_;
  Workspace ws_;
  uint32_t ppem_{0};
  uint32_t unitsPerEm_{0};
  uint32_t pointEnd_{0};
  uint32_t contourEnd_{0};
  uint32_t glyphCount_{0};
  bool hinting_{false};
  bool dirty_{false};
  bool ready_{false};
  bool hintedAny_{false};
  uint32_t fallbacks_{0};
};

} // namespace kernel::font::glyph

namespace kstd {
template <>
struct is_error_code_enum<kernel::font::glyph::Error> : true_type {};
} // namespace kstd
