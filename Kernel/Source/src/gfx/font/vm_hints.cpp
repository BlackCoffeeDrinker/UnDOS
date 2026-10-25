#include "vm_internal.hpp"

namespace kernel::font::vm::detail {
namespace {

// Returns false for a zero vector, which leaves the target vector unchanged.
bool normalize(int64_t x, int64_t y, Vector &out) {
  if (!x && !y) return false;
  // Scaling both components keeps the direction and bounds the squares below.
  while (x > NormalizeLimit || x < -NormalizeLimit || y > NormalizeLimit || y < -NormalizeLimit) {
    x >>= 1;
    y >>= 1;
  }
  if (!x && !y) return false;
  const int64_t length = isqrt(static_cast<uint64_t>(x * x + y * y) << NormalizeSquareShift);
  out = {.x = static_cast<int32_t>(divide_nearest(x * UnitVector * NormalizeFractionScale, length)), .y = static_cast<int32_t>(divide_nearest(y * UnitVector * NormalizeFractionScale, length))};
  return true;
}
}

kstd::error_code Executor::shift_point(Zone &zone, size_t index, int64_t dx, int64_t dy, bool touch) {
  const auto &geometry = state_->geometry;
  Point point = zone.current.data()[index];
  if (point.x + dx < INT32_MIN || point.x + dx > INT32_MAX ||
      point.y + dy < INT32_MIN || point.y + dy > INT32_MAX)
    return Error::ArithmeticOverflow;
  uint8_t touched = zone.touched.data()[index];
  if (geometry.freedom.x) { point.x = static_cast<int32_t>(point.x + dx); touched |= TouchedX; }
  if (geometry.freedom.y) { point.y = static_cast<int32_t>(point.y + dy); touched |= TouchedY; }
  zone.current.data()[index] = point;
  if (touch) zone.touched.data()[index] = touched;
  return Error::None;
}

// SPVTL, SFVTL and SDPVTL: the top operand indexes zp2 and the lower indexes zp1.
kstd::error_code Executor::point_vector(uint8_t op, int32_t lower, int32_t top) {
  auto &geometry = state_->geometry;
  Zone *first = nullptr, *second = nullptr;
  if (auto error = locate(geometry.zone[Zp1], lower, first); error) return error;
  if (auto error = locate(geometry.zone[Zp2], top, second); error) return error;
  const bool perpendicular = op & opcode::FlagOdd;
  const auto build = [&](const Point &a, const Point &b, Vector &out) {
    int64_t x = static_cast<int64_t>(a.x) - b.x, y = static_cast<int64_t>(a.y) - b.y;
    bool rotate = perpendicular;
    if (!x && !y) { x = UnitVector; y = 0; rotate = false; }
    if (rotate) { const int64_t c = y; y = x; x = -c; }
    return normalize(x, y, out);
  };
  Vector vector;
  if (op == opcode::SDPVTL0 || op == opcode::SDPVTL1) {
    if (build(first->original.data()[lower], second->original.data()[top], vector))
      geometry.dualProjection = vector;
    if (build(first->current.data()[lower], second->current.data()[top], vector))
      geometry.projection = vector;
    return Error::None;
  }
  if (!build(first->current.data()[lower], second->current.data()[top], vector)) return Error::None;
  if (op <= opcode::SPVTL1) geometry.projection = geometry.dualProjection = vector;
  else geometry.freedom = vector;
  return Error::None;
}

kstd::error_code Executor::vector_op(uint8_t op) {
  if (!state_) return Error::InvalidState;
  if (depth_ < 2) return Error::StackUnderflow;
  const int32_t lower = operand(depth_ - 2), top = operand(depth_ - 1);
  auto &geometry = state_->geometry;
  if (op == opcode::SPVFS || op == opcode::SFVFS) {
    Vector vector;
    if (normalize(static_cast<int16_t>(lower), static_cast<int16_t>(top), vector)) {
      if (op == opcode::SPVFS) geometry.projection = geometry.dualProjection = vector;
      else geometry.freedom = vector;
    }
  } else if (auto error = point_vector(op, lower, top); error) {
    return error;
  }
  depth_ -= 2;
  return Error::None;
}

// Selector layout follows the TrueType SROUND definition. Results are F26Dot6.
kstd::error_code Executor::set_super_round(uint8_t op) {
  if (!state_) return Error::InvalidState;
  if (!depth_) return Error::StackUnderflow;
  const int32_t selector = operand(--depth_);
  const bool diagonal = op == opcode::S45ROUND;
  const int32_t grid = diagonal ? Diagonal45Period : static_cast<int32_t>(SuperGridPeriod);
  int32_t period;
  switch (selector & SuperPeriodMask) {
    case SuperPeriodHalf: period = grid / 2; break;
    case SuperPeriodDouble: period = grid * 2; break;
    default: period = grid; break;
  }
  int32_t phase = 0;
  switch (selector & SuperPhaseMask) {
    case SuperPhaseQuarter: phase = period / 4; break;
    case SuperPhaseHalf: phase = period / 2; break;
    case SuperPhaseThreeQuarters: phase = period * 3 / 4; break;
    default: break;
  }
  const int32_t low = selector & SuperThresholdMask;
  const int32_t threshold =
      low ? (low - SuperThresholdBias) * period / SuperThresholdDivisor : period - 1;
  auto &parameters = state_->parameters;
  parameters.round = diagonal ? RoundMode::Super45 : RoundMode::Super;
  parameters.roundPeriod = period >> SuperFixedShift;
  parameters.roundPhase = phase >> SuperFixedShift;
  parameters.roundThreshold = threshold >> SuperFixedShift;
  return Error::None;
}

kstd::error_code Executor::untouch_point() {
  if (!state_) return Error::InvalidState;
  if (!depth_) return Error::StackUnderflow;
  Zone *zone = nullptr;
  const int32_t index = operand(depth_ - 1);
  if (auto error = locate(state_->geometry.zone[Zp0], index, zone); error) return error;
  uint8_t mask = TouchedBoth;
  if (state_->geometry.freedom.x) mask &= static_cast<uint8_t>(~TouchedX);
  if (state_->geometry.freedom.y) mask &= static_cast<uint8_t>(~TouchedY);
  zone->touched.data()[index] &= mask;
  --depth_;
  return Error::None;
}

// SHC shifts and touches one contour. SHZ shifts a zone without touching it.
kstd::error_code Executor::shift_contour_or_zone(uint8_t op) {
  if (!state_) return Error::InvalidState;
  if (!depth_) return Error::StackUnderflow;
  auto &geometry = state_->geometry;
  const int32_t argument = operand(depth_ - 1);
  const bool contour = op <= opcode::SHC1;
  const bool twilight = geometry.zone[Zp2] == TwilightZone;
  Zone *zone = twilight ? &state_->twilight : &state_->glyph;
  size_t begin = 0, end = zone->current.size();
  if (contour) {
    if (argument < 0) return Error::InvalidPoint;
    if (twilight) {
      if (argument != 0) return Error::InvalidPoint;
    } else {
      const auto &ends = state_->glyphContours;
      if (static_cast<uint32_t>(argument) >= ends.size()) return Error::InvalidPoint;
      begin = argument ? ends.data()[argument - 1] + 1 : 0;
      end = ends.data()[argument] + 1;
    }
  } else {
    if (argument < 0 || argument > 1) return Error::InvalidParameter; // Selector is 0 or 1.
    if (!twilight) {
      const auto &ends = state_->glyphContours;
      end = ends.size() ? ends.data()[ends.size() - 1] + 1 : 0;
    }
  }
  if (end > zone->current.size() || begin > end) return Error::InvalidPoint;
  int64_t dx, dy;
  uint32_t zoneId, refp;
  if (auto error = displacement(op, dx, dy, zoneId, refp); error) return error;
  for (size_t i = begin; i < end; ++i) {
    if (!charge(1)) return Error::WorkLimit;
    if (zoneId == geometry.zone[Zp2] && refp == i) continue;
    if (auto error = shift_point(*zone, i, dx, dy, contour); error) return error;
  }
  --depth_;
  return Error::None;
}

kstd::error_code Executor::intersect() {
  if (!state_) return Error::InvalidState;
  if (depth_ < IntersectOperands) return Error::StackUnderflow;
  auto &geometry = state_->geometry;
  const int32_t point = operand(depth_ - 5), a0 = operand(depth_ - 4), a1 = operand(depth_ - 3),
                b0 = operand(depth_ - 2), b1 = operand(depth_ - 1);
  Zone *target = nullptr, *lineA = nullptr, *lineB = nullptr;
  if (auto error = locate(geometry.zone[Zp2], point, target); error) return error;
  if (auto error = locate(geometry.zone[Zp1], a0, lineA); error) return error;
  if (auto error = locate(geometry.zone[Zp1], a1, lineA); error) return error;
  if (auto error = locate(geometry.zone[Zp0], b0, lineB); error) return error;
  if (auto error = locate(geometry.zone[Zp0], b1, lineB); error) return error;
  const Point pa0 = lineA->current.data()[a0], pa1 = lineA->current.data()[a1];
  const Point pb0 = lineB->current.data()[b0], pb1 = lineB->current.data()[b1];
  const int64_t dbx = static_cast<int64_t>(pb1.x) - pb0.x, dby = static_cast<int64_t>(pb1.y) - pb0.y;
  const int64_t dax = static_cast<int64_t>(pa1.x) - pa0.x, day = static_cast<int64_t>(pa1.y) - pa0.y;
  const int64_t dx = static_cast<int64_t>(pb0.x) - pa0.x, dy = static_cast<int64_t>(pb0.y) - pa0.y;
  const int64_t discriminant = divide_nearest(dax * -dby, IntersectScale) + divide_nearest(day * dbx, IntersectScale);
  const int64_t dot = divide_nearest(dax * dbx, IntersectScale) + divide_nearest(day * dby, IntersectScale);
  const auto magnitude = [](int64_t value) { return value < 0 ? -value : value; };
  int64_t x, y;
  if (discriminant && IntersectGrazingRatio * magnitude(discriminant) > magnitude(dot)) {
    const int64_t value = divide_nearest(dx * -dby, IntersectScale) + divide_nearest(dy * dbx, IntersectScale);
    x = pa0.x + divide_nearest(value * dax, discriminant);
    y = pa0.y + divide_nearest(value * day, discriminant);
  } else {
    x = (static_cast<int64_t>(pa0.x) + pa1.x + pb0.x + pb1.x) / IntersectAverageCount;
    y = (static_cast<int64_t>(pa0.y) + pa1.y + pb0.y + pb1.y) / IntersectAverageCount;
  }
  if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
    return Error::ArithmeticOverflow;
  target->current.data()[point] = {.x = static_cast<int32_t>(x), .y = static_cast<int32_t>(y)};
  target->touched.data()[point] |= TouchedBoth;
  depth_ -= IntersectOperands;
  return Error::None;
}

kstd::error_code Executor::align_points() {
  if (!state_) return Error::InvalidState;
  if (depth_ < 2) return Error::StackUnderflow;
  auto &geometry = state_->geometry;
  const int32_t p1 = operand(depth_ - 2), p2 = operand(depth_ - 1);
  Zone *zone1 = nullptr, *zone0 = nullptr;
  if (auto error = locate(geometry.zone[Zp1], p1, zone1); error) return error;
  if (auto error = locate(geometry.zone[Zp0], p2, zone0); error) return error;
  const Point a = zone1->current.data()[p1], b = zone0->current.data()[p2];
  const int64_t distance = project({.x = b.x - a.x, .y = b.y - a.y}) / 2;
  if (auto error = move_point(*zone1, static_cast<uint32_t>(p1), distance); error) return error;
  if (auto error = move_point(*zone0, static_cast<uint32_t>(p2), -distance); error) return error;
  depth_ -= 2;
  return Error::None;
}

// DELTAP and DELTAC: each pair is an argument byte below a point or CVT index.
// Only pairs whose ppem matches the encoded size apply.
kstd::error_code Executor::delta_points(uint8_t op) { return delta_apply(op, false); }
kstd::error_code Executor::delta_cvt(uint8_t op) { return delta_apply(op, true); }

kstd::error_code Executor::delta_apply(uint8_t op, bool cvt) {
  if (!state_) return Error::InvalidState;
  if (!depth_) return Error::StackUnderflow;
  const int32_t count = operand(depth_ - 1);
  if (count < 0 || static_cast<uint32_t>(count) > MaxWork) return Error::InvalidParameter;
  if (depth_ - 1 < static_cast<uint32_t>(count) * 2) return Error::StackUnderflow;
  const uint32_t variant = cvt ? op - opcode::DELTAC1 : (op == opcode::DELTAP1 ? 0 : op - opcode::WCVTF);
  --depth_;
  const auto &parameters = state_->parameters;
  for (int32_t i = 0; i < count; ++i) {
    if (!charge(1)) return Error::WorkLimit;
    const int32_t index = operand(--depth_);
    const uint32_t argument = static_cast<uint32_t>(operand(--depth_));
    const uint32_t size = ((argument & DeltaPpemMask) >> DeltaPpemShift) + variant * DeltaPpemRange +
        parameters.deltaBase;
    if (size != state_->ppem) {
      Zone *zone = nullptr;
      if (cvt ? (index < 0 || static_cast<uint32_t>(index) >= state_->cvt.size()) :
                locate(state_->geometry.zone[Zp0], index, zone) != Error::None)
        return cvt ? Error::InvalidCvtIndex : Error::InvalidPoint;
      continue;
    }
    if (parameters.deltaShift > MaxDeltaShift) return Error::InvalidParameter;
    int32_t step = static_cast<int32_t>(argument & DeltaStepMask) - DeltaStepBias;
    if (step >= 0) ++step;
    step *= 1 << (MaxDeltaShift - parameters.deltaShift);
    if (cvt) {
      if (index < 0 || static_cast<uint32_t>(index) >= state_->cvt.size()) return Error::InvalidCvtIndex;
      const int64_t value = static_cast<int64_t>(state_->cvt.data()[index]) + step;
      if (value < INT32_MIN || value > INT32_MAX) return Error::ArithmeticOverflow;
      state_->cvt.data()[index] = static_cast<int32_t>(value);
    } else {
      Zone *zone = nullptr;
      if (auto error = locate(state_->geometry.zone[Zp0], index, zone); error) return error;
      if (auto error = move_point(*zone, static_cast<uint32_t>(index), step); error) return error;
    }
  }
  return Error::None;
}

// FLIPPT toggles loop points. FLIPRGON and FLIPRGOFF set an inclusive range.
kstd::error_code Executor::flip_points(uint8_t op) {
  if (!state_) return Error::InvalidState;
  auto &glyph = state_->glyph;
  if (glyph.onCurve.empty()) return Error::InvalidState;
  const size_t size = glyph.onCurve.size();
  if (op == opcode::FLIPPT) {
    const uint32_t loop = state_->geometry.loop;
    if (depth_ < loop) return Error::StackUnderflow;
    for (uint32_t i = 0; i < loop; ++i) {
      if (!charge(1)) return Error::WorkLimit;
      const int32_t index = operand(--depth_);
      if (index < 0 || static_cast<uint32_t>(index) >= size) return Error::InvalidPoint;
      auto &flag = glyph.onCurve.data()[index];
      flag = !flag;
    }
    state_->geometry.loop = DefaultLoop;
    return Error::None;
  }
  if (depth_ < 2) return Error::StackUnderflow;
  const int32_t first = operand(depth_ - 2), last = operand(depth_ - 1);
  if (first < 0 || last < 0 || static_cast<uint32_t>(first) >= size || static_cast<uint32_t>(last) >= size)
    return Error::InvalidPoint;
  for (int32_t i = first; i <= last; ++i) {
    if (!charge(1)) return Error::WorkLimit;
    glyph.onCurve.data()[i] = op == opcode::FLIPRGON ? 1 : 0;
  }
  depth_ -= 2;
  return Error::None;
}
}
