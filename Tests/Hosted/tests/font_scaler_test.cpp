#include <undos/font/scaler.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace undos::font;
}

TEST_CASE("font scaler rounds signed values to nearest 26.6 with ties away", "[font][scaler]") {
  struct Example {
    int32_t value;
    uint32_t ppem, units;
    int32_t expected;
  };
  const Example examples[] = {
      {1, 1, 128, 1}, {-1, 1, 128, -1},
      {1, 1, 129, 0}, {-1, 1, 129, 0},
      {3, 1, 128, 2}, {-3, 1, 128, -2},
      {750, 16, 1000, 768}, {-250, 16, 1000, -256},
      {1000, 16, 1000, 1024}, {-326, 16, 1000, -334},
      {INT32_MIN, 1, 64, INT32_MIN}, {INT32_MAX, 1, 64, INT32_MAX},
      {0, 256, 16, 0},
  };
  for (const auto &example : examples) {
    int32_t output = 123;
    REQUIRE(scale_funits(example.value, example.ppem, example.units, output) == ScaleError::None);
    REQUIRE(output == example.expected);
  }
}

TEST_CASE("font scaler meets its error bound throughout the ppem profile", "[font][scaler]") {
  const int32_t values[] = {-32768, -1000, -1, 0, 1, 1000, 32767};
  const uint32_t units[] = {16, 999, 1000, 2048, 16384};
  for (uint32_t ppem = MinPpem; ppem <= MaxPpem; ++ppem) {
    for (const auto unit : units) {
      for (const auto value : values) {
        int32_t output;
        REQUIRE(scale_funits(value, ppem, unit, output) == ScaleError::None);
        const int64_t exact = int64_t(value) * ppem * 64;
        const int64_t difference = int64_t(output) * unit - exact;
        const int64_t distance = difference < 0 ? -difference : difference;
        REQUIRE(distance * 2 <= unit);
        if (distance * 2 == unit)
          REQUIRE((value < 0 ? int64_t(output) * unit < exact : int64_t(output) * unit > exact));
      }
    }
  }
}

TEST_CASE("font scaler rejects unsupported profiles and overflow without changing output", "[font][scaler]") {
  int32_t output = 123;
  REQUIRE(scale_funits(1, 0, 1000, output) == ScaleError::InvalidScale);
  REQUIRE(scale_funits(1, 257, 1000, output) == ScaleError::InvalidScale);
  REQUIRE(scale_funits(1, 16, 0, output) == ScaleError::InvalidScale);
  REQUIRE(scale_funits(1, 16, 15, output) == ScaleError::InvalidScale);
  REQUIRE(scale_funits(1, 16, 16385, output) == ScaleError::InvalidScale);
  REQUIRE(scale_funits(INT32_MAX, 256, 16, output) == ScaleError::Overflow);
  REQUIRE(scale_funits(INT32_MIN, 256, 16, output) == ScaleError::Overflow);
  REQUIRE(output == 123);
}

TEST_CASE("font line scaler rounds extents outward and preserves gap below descent", "[font][scaler]") {
  PixelLineMetrics output{};
  REQUIRE(scale_line_metrics({750, -250, 0}, 16, 1000, output) == ScaleError::None);
  REQUIRE(output.ascent == 12);
  REQUIRE(output.descent == 4);
  REQUIRE(output.lineGap == 0);
  REQUIRE(output.height == 16);
  REQUIRE(scale_line_metrics({1000, -326, 0}, 16, 1000, output) == ScaleError::None);
  REQUIRE(output.ascent == 16);
  REQUIRE(output.descent == 6);
  REQUIRE(output.height == 22);
  REQUIRE(scale_line_metrics({800, -200, 50}, 16, 1024, output) == ScaleError::None);
  REQUIRE(output.ascent == 13);
  REQUIRE(output.descent == 4);
  REQUIRE(output.lineGap == 1);
  REQUIRE(output.height == 18);
  REQUIRE(scale_line_metrics({0, INT32_MIN, 0}, 1, 64, output) == ScaleError::None);
  REQUIRE(output.ascent == 0);
  REQUIRE(output.descent == 33554432);
  REQUIRE(output.height == 33554432);
}

TEST_CASE("font line scaler rejects invalid metrics and zero scaled line height", "[font][scaler]") {
  PixelLineMetrics output{1, 2, 3, 6};
  REQUIRE(scale_line_metrics({-1, -200, 0}, 16, 1000, output) == ScaleError::InvalidMetrics);
  REQUIRE(scale_line_metrics({800, 1, 0}, 16, 1000, output) == ScaleError::InvalidMetrics);
  REQUIRE(scale_line_metrics({800, -200, -1}, 16, 1000, output) == ScaleError::InvalidMetrics);
  REQUIRE(scale_line_metrics({0, 0, 1}, 16, 1000, output) == ScaleError::InvalidMetrics);
  REQUIRE(scale_line_metrics({1, 0, 0}, 1, 16384, output) == ScaleError::InvalidMetrics);
  REQUIRE(scale_line_metrics({800, -200, 0}, 0, 1000, output) == ScaleError::InvalidScale);
  REQUIRE(scale_line_metrics({INT32_MAX, -200, 0}, 256, 16, output) == ScaleError::Overflow);
  REQUIRE(output.ascent == 1);
  REQUIRE(output.descent == 2);
  REQUIRE(output.lineGap == 3);
  REQUIRE(output.height == 6);
}
