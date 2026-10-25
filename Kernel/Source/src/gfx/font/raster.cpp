#include "raster.hpp"

#include "vm_internal.hpp"

#include <new.hpp>

namespace kernel::font::raster {
namespace {
constexpr int32_t PixelBits = 6;
constexpr int64_t Pixel = 1 << PixelBits;
constexpr int64_t HalfPixel = Pixel / 2;
// Deviation allowed per curve, in 1/64 pixel, before another segment is added.
constexpr int64_t CurveTolerance = 8;
constexpr int64_t DeviationDivisor = 4 * CurveTolerance;
constexpr int32_t CoordinateLimit = 1 << 28;

int64_t floor_div(int64_t value, int64_t divisor) {
  int64_t quotient = value / divisor;
  if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) --quotient;
  return quotient;
}

int64_t ceil_div(int64_t value, int64_t divisor) { return -floor_div(-value, divisor); }

int64_t absolute(int64_t value) { return value < 0 ? -value : value; }

struct Vec {
  int32_t x, y;
};

// Collects line segments from TrueType contours, flattening quadratic curves.
class Flattener {
public:
  explicit Flattener(kstd::span<Edge> edges) : edges_(edges) {}

  bool failed() const { return failed_; }
  size_t count() const { return count_; }

  void line(Vec a, Vec b) {
    if (a.y == b.y) return;
    if (count_ >= edges_.size()) {
      failed_ = true;
      return;
    }
    edges_.data()[count_++] = {.x0 = a.x, .y0 = a.y, .x1 = b.x, .y1 = b.y};
  }

  void quad(Vec a, Vec c, Vec b) {
    const int64_t deviation = absolute(static_cast<int64_t>(a.x) - 2 * static_cast<int64_t>(c.x) + b.x) +
                              absolute(static_cast<int64_t>(a.y) - 2 * static_cast<int64_t>(c.y) + b.y);
    int64_t segments = vm::detail::isqrt(static_cast<uint64_t>(deviation / DeviationDivisor)) + 1;
    if (segments > MaxCurveSegments) segments = MaxCurveSegments;
    Vec previous = a;
    for (int64_t i = 1; i <= segments; ++i) {
      Vec next = b;
      if (i < segments) {
        const int64_t w0 = (segments - i) * (segments - i), w1 = 2 * i * (segments - i), w2 = i * i;
        const int64_t total = segments * segments;
        next.x = static_cast<int32_t>(floor_div(w0 * a.x + w1 * c.x + w2 * b.x + total / 2, total));
        next.y = static_cast<int32_t>(floor_div(w0 * a.y + w1 * c.y + w2 * b.y + total / 2, total));
      }
      line(previous, next);
      previous = next;
    }
  }

private:
  kstd::span<Edge> edges_;
  size_t count_{0};
  bool failed_{false};
};

Vec midpoint(Vec a, Vec b) { return {.x = (a.x + b.x) >> 1, .y = (a.y + b.y) >> 1}; }

// Walks one contour of on and off curve points; off-curve neighbours imply an
// on-curve point halfway between them.
void flatten_contour(Flattener &out, const glyph::Outline &outline, size_t first, size_t last) {
  const auto point = [&](size_t i) { return Vec{.x = outline.points.data()[i].x, .y = outline.points.data()[i].y}; };
  const auto on = [&](size_t i) { return outline.onCurve.data()[i] != 0; };
  Vec start;
  size_t from = first, to = last + 1;
  if (on(first)) {
    start = point(first);
    ++from;
  } else if (on(last)) {
    start = point(last);
    --to;
  } else {
    start = midpoint(point(first), point(last));
  }
  Vec current = start, control{.x = 0, .y = 0};
  bool haveControl = false;
  for (size_t i = from; i < to; ++i) {
    const Vec q = point(i);
    if (on(i)) {
      if (haveControl) out.quad(current, control, q);
      else out.line(current, q);
      haveControl = false;
      current = q;
    } else if (haveControl) {
      const Vec middle = midpoint(control, q);
      out.quad(current, control, middle);
      current = middle;
      control = q;
    } else {
      control = q;
      haveControl = true;
    }
  }
  if (haveControl) out.quad(current, control, start);
  else out.line(current, start);
}

kstd::error_code validate_outline(const glyph::Outline &outline) {
  const size_t count = outline.points.size();
  if (outline.onCurve.size() != count) return Error::InvalidOutline;
  size_t previous = 0;
  for (size_t i = 0; i < outline.contourEnds.size(); ++i) {
    const size_t end = outline.contourEnds.data()[i];
    if (end >= count || (i && end <= previous)) return Error::InvalidOutline;
    previous = end;
  }
  for (size_t i = 0; i < count; ++i) {
    const auto &p = outline.points.data()[i];
    if (p.x < -CoordinateLimit || p.x > CoordinateLimit || p.y < -CoordinateLimit || p.y > CoordinateLimit)
      return Error::InvalidOutline;
  }
  return Error::None;
}

void set_pixel(uint8_t *row, int64_t x, uint32_t width) {
  if (x < 0 || x >= width) return;
  row[x >> 3] = static_cast<uint8_t>(row[x >> 3] | (0x80u >> (x & 7)));
}

// Sets pixels whose centres lie in [xa, xb) (F26Dot6, relative to the bitmap).
void fill_span(uint8_t *row, int64_t xa, int64_t xb, uint32_t width) {
  int64_t first = floor_div(xa + HalfPixel - 1, Pixel);
  int64_t end = floor_div(xb + HalfPixel - 1, Pixel);
  if (first >= end) {
    if (xb <= xa) return;
    set_pixel(row, floor_div(xa + xb, 2 * Pixel), width);
    return;
  }
  if (first < 0) first = 0;
  if (end > width) end = width;
  for (int64_t x = first; x < end; ++x) set_pixel(row, x, width);
}
} // namespace

namespace {
class RasterCategory final : public kstd::error_category {
public:
  [[nodiscard]] const char *name() const noexcept override { return "font_raster_category"; }
  [[nodiscard]] kstd::string_view message(int value) const noexcept override {
    switch (static_cast<Error>(value)) {
      case Error::None: return "success";
      case Error::InvalidOutline: return "outline arrays or coordinates invalid";
      case Error::InvalidWorkspace: return "raster workspace too small";
      case Error::EdgeLimit: return "outline needs more edges than the workspace holds";
      case Error::BitmapTooLarge: return "bitmap exceeds the size limit or the buffer";
    }
    return "unknown raster error";
  }
};
}

const kstd::error_category &raster_category() noexcept {
  // Error codes keep this reference; never register its destructor.
  alignas(RasterCategory) static unsigned char storage[sizeof(RasterCategory)];
  static const RasterCategory &category = *new (storage) RasterCategory;
  return category;
}

kstd::error_code measure(const glyph::Outline &outline, Bounds &bounds) {
  bounds = {};
  if (const auto error = validate_outline(outline); error) return error;
  if (outline.points.size() == 0) return Error::None;
  int64_t minX = outline.points.data()[0].x, maxX = minX;
  int64_t minY = outline.points.data()[0].y, maxY = minY;
  for (size_t i = 1; i < outline.points.size(); ++i) {
    const auto &p = outline.points.data()[i];
    if (p.x < minX) minX = p.x;
    if (p.x > maxX) maxX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.y > maxY) maxY = p.y;
  }
  const int64_t left = floor_div(minX, Pixel), right = ceil_div(maxX, Pixel);
  const int64_t bottom = floor_div(minY, Pixel), top = ceil_div(maxY, Pixel);
  if (right - left > MaxBitmapSide || top - bottom > MaxBitmapSide) return Error::BitmapTooLarge;
  bounds.left = static_cast<int32_t>(left);
  bounds.bottom = static_cast<int32_t>(bottom);
  bounds.width = static_cast<uint32_t>(right - left);
  bounds.height = static_cast<uint32_t>(top - bottom);
  return Error::None;
}

kstd::error_code render(const glyph::Outline &outline, const Workspace &workspace,
                        kstd::span<uint8_t> bits, Bitmap &bitmap) {
  bitmap = {};
  Bounds bounds;
  if (const auto error = measure(outline, bounds); error) return error;
  const uint32_t stride = stride_for(bounds.width);
  if (static_cast<uint64_t>(stride) * bounds.height > bits.size()) return Error::BitmapTooLarge;
  for (size_t i = 0; i < static_cast<size_t>(stride) * bounds.height; ++i) bits.data()[i] = 0;
  bitmap.left = bounds.left;
  bitmap.top = bounds.bottom + static_cast<int32_t>(bounds.height) - (bounds.height ? 1 : 0);
  bitmap.width = bounds.width;
  bitmap.height = bounds.height;
  bitmap.stride = stride;
  bitmap.bits = {bits.data(), static_cast<size_t>(stride) * bounds.height};
  if (!bounds.height) return Error::None;

  Flattener flattener(workspace.edges);
  size_t first = 0;
  for (size_t c = 0; c < outline.contourEnds.size(); ++c) {
    const size_t last = outline.contourEnds.data()[c];
    flatten_contour(flattener, outline, first, last);
    first = last + 1;
  }
  if (flattener.failed()) return Error::EdgeLimit;
  const size_t edgeCount = flattener.count();
  if (workspace.crossings.size() < edgeCount) return Error::InvalidWorkspace;

  const int64_t leftUnits = static_cast<int64_t>(bounds.left) * Pixel;
  const auto gather = [&](int64_t sampleY) {
    size_t crossings = 0;
    auto *list = workspace.crossings.data();
    for (size_t e = 0; e < edgeCount; ++e) {
      const Edge &edge = workspace.edges.data()[e];
      const bool upward = edge.y1 > edge.y0;
      const int64_t xa = upward ? edge.x0 : edge.x1, ya = upward ? edge.y0 : edge.y1;
      const int64_t xb = upward ? edge.x1 : edge.x0, yb = upward ? edge.y1 : edge.y0;
      if (sampleY < ya || sampleY >= yb) continue;
      const int64_t dy = yb - ya;
      const int64_t x = xa + floor_div(2 * (sampleY - ya) * (xb - xa) + dy, 2 * dy) - leftUnits;
      size_t at = crossings++;
      while (at > 0 && list[at - 1].x > x) {
        list[at] = list[at - 1];
        --at;
      }
      list[at] = {.x = static_cast<int32_t>(x), .winding = upward ? 1 : -1};
    }
    return crossings;
  };
  // Calls visit(xa, xb) for each span with non-zero winding.
  const auto spans = [&](int64_t sampleY, auto &&visit) {
    const size_t crossings = gather(sampleY);
    int32_t winding = 0;
    int64_t spanStart = 0;
    for (size_t i = 0; i < crossings; ++i) {
      const Crossing &crossing = workspace.crossings.data()[i];
      if (winding == 0) spanStart = crossing.x;
      winding += crossing.winding;
      if (winding == 0) visit(spanStart, static_cast<int64_t>(crossing.x));
    }
  };

  const int64_t topY = bitmap.top;
  for (uint32_t row = 0; row < bounds.height; ++row) {
    uint8_t *line = bits.data() + static_cast<size_t>(row) * stride;
    spans((topY - row) * Pixel + HalfPixel, [&](int64_t xa, int64_t xb) { fill_span(line, xa, xb, bounds.width); });
  }
  return Error::None;
}

} // namespace kernel::font::raster
