#include <undos/font/scaler.hpp>

namespace undos::font {

ScaleError scale_funits(int32_t value, uint32_t ppem, uint32_t unitsPerEm,
                        int32_t &output) {
  if (ppem < MinPpem || ppem > MaxPpem || unitsPerEm < 16 || unitsPerEm > 16384)
    return ScaleError::InvalidScale;
  const int64_t numerator = int64_t(value) * ppem * 64;
  const int64_t magnitude = numerator < 0 ? -numerator : numerator;
  const int64_t rounded = (magnitude + unitsPerEm / 2) / unitsPerEm;
  const int64_t result = numerator < 0 ? -rounded : rounded;
  if (result < INT32_MIN || result > INT32_MAX) return ScaleError::Overflow;
  output = int32_t(result);
  return ScaleError::None;
}

ScaleError scale_line_metrics(const SourceLineMetrics &source, uint32_t ppem,
                              uint32_t unitsPerEm, PixelLineMetrics &output) {
  if (source.ascent < 0 || source.descent > 0 || source.lineGap < 0 ||
      int64_t(source.ascent) - source.descent <= 0)
    return ScaleError::InvalidMetrics;
  int32_t ascent, descent, gap;
  auto error = scale_funits(source.ascent, ppem, unitsPerEm, ascent);
  if (error != ScaleError::None) return error;
  error = scale_funits(source.descent, ppem, unitsPerEm, descent);
  if (error != ScaleError::None) return error;
  error = scale_funits(source.lineGap, ppem, unitsPerEm, gap);
  if (error != ScaleError::None) return error;
  PixelLineMetrics result{
      uint32_t((int64_t(ascent) + 63) / 64),
      uint32_t((-int64_t(descent) + 63) / 64),
      uint32_t((int64_t(gap) + 63) / 64),
      0};
  result.height = result.ascent + result.descent + result.lineGap;
  if (!result.height) return ScaleError::InvalidMetrics;
  output = result;
  return ScaleError::None;
}

} // namespace undos::font
