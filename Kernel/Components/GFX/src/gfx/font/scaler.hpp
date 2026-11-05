#pragma once

#include <stdint.h>

namespace undos::font {

constexpr uint32_t MinPpem = 1;
constexpr uint32_t MaxPpem = 256;

enum class ScaleError {
  None, InvalidScale, Overflow, InvalidMetrics
};

struct SourceLineMetrics {
  int32_t ascent;
  int32_t descent;
  int32_t lineGap;
};

struct PixelLineMetrics {
  uint32_t ascent;
  uint32_t descent;
  uint32_t lineGap;
  uint32_t height;
};

// Equal-axis integer ppem, 16..16384 units/em. Round to signed 26.6,
// nearest with ties away from zero. Leave output unchanged on error.
ScaleError scale_funits(int32_t value, uint32_t ppem, uint32_t unitsPerEm,
                        int32_t &output);

// Callers select OS/2 typo metrics, or hhea only if OS/2 is absent.
// Round each scaled extent outward to pixels; place line gap below descent.
// Leave output unchanged on error, including a zero-height scaled line.
ScaleError scale_line_metrics(const SourceLineMetrics &source, uint32_t ppem,
                              uint32_t unitsPerEm, PixelLineMetrics &output);

} // namespace undos::font
