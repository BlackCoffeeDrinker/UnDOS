#include "gfx/font/raster.hpp"
#include <catch2/catch_test_macros.hpp>
#include <vector.hpp>

namespace {
using namespace kernel::font;

struct Shape {
  kstd::vector<vm::Point> points;
  kstd::vector<uint8_t> onCurve;
  kstd::vector<uint32_t> ends;

  // Coordinates are F26Dot6.
  void add(int32_t x, int32_t y, bool on = true) {
    points.push_back({x, y});
    onCurve.push_back(on ? 1 : 0);
  }
  void close() { ends.push_back(static_cast<uint32_t>(points.size() - 1)); }
  glyph::Outline outline() const {
    glyph::Outline o;
    o.points = {points.data(), points.size()};
    o.onCurve = {onCurve.data(), onCurve.size()};
    o.contourEnds = {ends.data(), ends.size()};
    return o;
  }
};

struct Result {
  kstd::error_code error;
  raster::Bitmap bitmap;
  kstd::vector<uint8_t> bits;
  kstd::vector<raster::Edge> edges;
  kstd::vector<raster::Crossing> crossings;

  bool pixel(uint32_t x, uint32_t row) const {
    return bitmap.bits.data()[row * bitmap.stride + x / 8] & (0x80 >> (x % 8));
  }
  // One character per pixel, rows separated by newlines.
  kstd::vector<char> text() const {
    kstd::vector<char> out;
    for (uint32_t r = 0; r < bitmap.height; ++r) {
      for (uint32_t x = 0; x < bitmap.width; ++x) out.push_back(pixel(x, r) ? '#' : '.');
      out.push_back('\n');
    }
    return out;
  }
};

bool equals(const kstd::vector<char> &text, const char *expected) {
  size_t i = 0;
  for (; expected[i]; ++i)
    if (i >= text.size() || text[i] != expected[i]) return false;
  return i == text.size();
}

Result draw(const Shape &shape, size_t edgeCapacity = 0) {
  Result r;
  const auto outline = shape.outline();
  raster::Bounds bounds;
  const auto measured = raster::measure(outline, bounds);
  if (measured) {
    r.error = measured;
    return r;
  }
  r.bits.resize(raster::stride_for(bounds.width) * bounds.height + 1);
  const size_t capacity = edgeCapacity ? edgeCapacity : raster::edge_capacity(shape.points.size()) + 1;
  r.edges.resize(capacity);
  r.crossings.resize(capacity);
  r.error = raster::render(outline, {{r.edges.data(), r.edges.size()}, {r.crossings.data(), r.crossings.size()}},
                           {r.bits.data(), r.bits.size()}, r.bitmap);
  return r;
}

void rectangle(Shape &s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, bool counterClockwise) {
  if (counterClockwise) {
    s.add(x0, y0); s.add(x1, y0); s.add(x1, y1); s.add(x0, y1);
  } else {
    s.add(x0, y0); s.add(x0, y1); s.add(x1, y1); s.add(x1, y0);
  }
  s.close();
}
} // namespace

TEST_CASE("rasterizer fills whole-pixel rectangles", "[font][raster]") {
  Shape s;
  rectangle(s, 64, 64, 256, 192, true);
  const auto r = draw(s);
  REQUIRE(r.error == kstd::error_code{});
  REQUIRE(r.bitmap.left == 1);
  REQUIRE(r.bitmap.top == 2);
  REQUIRE(r.bitmap.width == 3);
  REQUIRE(r.bitmap.height == 2);
  REQUIRE(equals(r.text(), "###\n###\n"));
}

TEST_CASE("rasterizer samples pixel centres", "[font][raster]") {
  Shape s;
  // Covers x 0.25..2.75 and y 0..2: the centres of pixels 0, 1 and 2 are inside.
  rectangle(s, 16, 0, 176, 128, true);
  auto r = draw(s);
  REQUIRE(equals(r.text(), "###\n###\n"));
  Shape narrow;
  rectangle(narrow, 40, 0, 88, 64, true); // x 0.625..1.375 holds only the centre of pixel 1.
  r = draw(narrow);
  REQUIRE(r.bitmap.left == 0);
  REQUIRE(equals(r.text(), ".#\n"));
}

TEST_CASE("rasterizer cuts holes with opposite winding and keeps overlaps filled", "[font][raster]") {
  Shape ring;
  rectangle(ring, 0, 0, 320, 320, true);
  rectangle(ring, 64, 64, 256, 256, false);
  auto r = draw(ring);
  REQUIRE(equals(r.text(), "#####\n#...#\n#...#\n#...#\n#####\n"));

  Shape overlap;
  rectangle(overlap, 0, 0, 192, 192, true);
  rectangle(overlap, 64, 64, 256, 256, true);
  r = draw(overlap);
  REQUIRE(r.bitmap.width == 4);
  REQUIRE(equals(r.text(), ".###\n####\n####\n###.\n"));
}

TEST_CASE("rasterizer fills triangles by centre sampling", "[font][raster]") {
  Shape s;
  s.add(0, 0);
  s.add(256, 0);
  s.add(0, 256);
  s.close();
  const auto r = draw(s);
  // Row centres 3.5, 2.5, 1.5 and 0.5 give spans ending at x 0.5, 1.5, 2.5 and 3.5.
  // The first span holds no centre, so dropout control sets pixel 0.
  REQUIRE(equals(r.text(), "#...\n#...\n##..\n###.\n"));
}

TEST_CASE("rasterizer flattens quadratic curves and implied on-curve points", "[font][raster]") {
  // A circle of radius 4 pixels from four off-curve points: every on-curve
  // point is implied.
  Shape circle;
  const int32_t r = 256;
  const int32_t k = 256;
  circle.add(-r, -k, false);
  circle.add(r, -k, false);
  circle.add(r, k, false);
  circle.add(-r, k, false);
  circle.close();
  const auto out = draw(circle);
  REQUIRE(out.error == kstd::error_code{});
  REQUIRE(out.bitmap.width == 8);
  REQUIRE(out.bitmap.height == 8);
  // The corners stay empty and the middle is solid. The shape is symmetric.
  REQUIRE(out.pixel(3, 3));
  REQUIRE(out.pixel(4, 4));
  REQUIRE_FALSE(out.pixel(0, 0));
  REQUIRE_FALSE(out.pixel(7, 7));
  for (uint32_t y = 0; y < 8; ++y)
    for (uint32_t x = 0; x < 8; ++x) {
      REQUIRE(out.pixel(x, y) == out.pixel(7 - x, y));
      REQUIRE(out.pixel(x, y) == out.pixel(x, 7 - y));
    }
}

TEST_CASE("rasterizer handles contours that start off the curve", "[font][raster]") {
  Shape s;
  s.add(0, 0, false);
  s.add(256, 0);
  s.add(256, 256);
  s.add(0, 256);
  s.close();
  const auto r = draw(s);
  REQUIRE(r.error == kstd::error_code{});
  REQUIRE(r.pixel(3, 0));
  REQUIRE(r.pixel(3, 3));
  REQUIRE(r.pixel(1, 1));
}

TEST_CASE("rasterizer keeps thin vertical stems visible", "[font][raster]") {
  Shape s;
  // 2/64 pixel wide, between pixel centres: no centre is inside. Dropout control keeps one pixel.
  rectangle(s, 70, 0, 72, 64, true);
  const auto r = draw(s);
  REQUIRE(r.error == kstd::error_code{});
  REQUIRE(r.bitmap.left == 1);
  REQUIRE(r.bitmap.width == 1);
  REQUIRE(r.pixel(0, 0));
}

TEST_CASE("rasterizer renders empty outlines and reports limits", "[font][raster]") {
  Shape empty;
  auto r = draw(empty);
  REQUIRE(r.error == kstd::error_code{});
  REQUIRE(r.bitmap.width == 0);
  REQUIRE(r.bitmap.height == 0);

  Shape s;
  rectangle(s, 0, 0, 64, 64, true);
  r = draw(s, 2); // Only the two vertical edges count.
  REQUIRE(r.error == kstd::error_code{});
  Shape triangle;
  triangle.add(0, 0);
  triangle.add(64, 0);
  triangle.add(0, 64);
  triangle.close();
  r = draw(triangle, 1);
  REQUIRE(r.error == raster::Error::EdgeLimit);

  Shape big;
  rectangle(big, 0, 0, 64 * (raster::MaxBitmapSide + 1), 64, true);
  r = draw(big);
  REQUIRE(r.error == raster::Error::BitmapTooLarge);

  Shape bad;
  rectangle(bad, 0, 0, 64, 64, true);
  bad.ends[0] = 9;
  r = draw(bad);
  REQUIRE(r.error == raster::Error::InvalidOutline);

  Shape small;
  rectangle(small, 0, 0, 128, 128, true);
  const auto outline = small.outline();
  kstd::vector<uint8_t> bits(1);
  kstd::vector<raster::Edge> edges(8);
  kstd::vector<raster::Crossing> crossings(8);
  raster::Bitmap bitmap;
  REQUIRE(raster::render(outline, {{edges.data(), 8}, {crossings.data(), 8}}, {bits.data(), 1}, bitmap) ==
          raster::Error::BitmapTooLarge);
  bits.resize(2);
  REQUIRE(raster::render(outline, {{edges.data(), 8}, {crossings.data(), 1}}, {bits.data(), 2}, bitmap) ==
          raster::Error::InvalidWorkspace);
}
