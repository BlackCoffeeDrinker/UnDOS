#pragma once

#include "glyph.hpp"

namespace kernel::font::raster {

// Quadratic curves are split into at most this many line segments. Splitting
// keeps the deviation from the curve at or below 1/8 pixel for ordinary sizes.
constexpr uint32_t MaxCurveSegments = 16;
// Worst-case edge count for an outline: every point may start a curve.
constexpr size_t edge_capacity(size_t points) { return points * MaxCurveSegments; }
// Largest bitmap side in pixels.
constexpr uint32_t MaxBitmapSide = 2048;

// Error codes are values of RasterCategory. None is zero.
enum class Error : int { None = 0, InvalidOutline, InvalidWorkspace, EdgeLimit, BitmapTooLarge };

const kstd::error_category &raster_category() noexcept;

inline kstd::error_code make_error_code(Error error) noexcept {
  if (error == Error::None) return {};
  return kstd::error_code(static_cast<int>(error), raster_category());
}

// A line segment in F26Dot6 pixels, kept in its original direction.
struct Edge {
  int32_t x0{0}, y0{0}, x1{0}, y1{0};
};

// A scanline crossing. winding is +1 for an upward edge and -1 for a downward one.
struct Crossing {
  int32_t x{0};
  int32_t winding{0};
};

// Caller-owned scratch. edges needs edge_capacity(points) entries for the worst
// case. crossings needs as many entries as edges.
struct Workspace {
  kstd::span<Edge> edges;
  kstd::span<Crossing> crossings;
};

// Pixel box of an outline. left and bottom are the lowest covered pixel indices
// relative to the pen origin, with y growing upward. An empty outline gives
// zero width and height.
struct Bounds {
  int32_t left{0};
  int32_t bottom{0};
  uint32_t width{0};
  uint32_t height{0};
};

// A 1 bit per pixel bitmap. Row 0 is the top row and bit 7 of each byte is the
// leftmost pixel. top is the y of the top row, so the bottom row is top - height + 1.
struct Bitmap {
  int32_t left{0};
  int32_t top{0};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t stride{0};
  kstd::span<uint8_t> bits{};
};

constexpr uint32_t stride_for(uint32_t width) { return (width + 7u) / 8u; }

// Computes the pixel box from the control points. The box is conservative:
// curves never leave the hull of their control points.
kstd::error_code measure(const glyph::Outline &outline, Bounds &bounds);

// Fills the bitmap from the outline with non-zero winding. A pixel is set when
// its centre is inside the outline. A span that holds no pixel centre sets the
// pixel under its middle. There is no vertical dropout control: a feature
// thinner than a pixel row that no scanline centre crosses can vanish. The
// caller sizes bits to stride_for(width) * height bytes from measure(). Work
// is bounded by height * edges. The bitmap is cleared before drawing.
kstd::error_code render(const glyph::Outline &outline, const Workspace &workspace,
                        kstd::span<uint8_t> bits, Bitmap &bitmap);

} // namespace kernel::font::raster

namespace kstd {
template <>
struct is_error_code_enum<kernel::font::raster::Error> : true_type {};
} // namespace kstd
