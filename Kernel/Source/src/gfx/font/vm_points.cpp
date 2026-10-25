#include "vm_internal.hpp"

namespace kernel::font::vm::detail {
kstd::error_code Executor::locate(uint32_t zoneId, int32_t index, Zone *&zone) const {
  zone = zoneId == TwilightZone ? &state_->twilight : &state_->glyph;
  return index >= 0 && static_cast<uint32_t>(index) < zone->current.size() ? Error::None : Error::InvalidPoint;
}

int64_t Executor::movement_scale() const {
  const auto &geometry = state_->geometry;
  const int64_t scale = (static_cast<int64_t>(geometry.freedom.x) * geometry.projection.x +
                         static_cast<int64_t>(geometry.freedom.y) * geometry.projection.y + UnitRounding) >> UnitBits;
  return scale > -MinFreedomProjection && scale < MinFreedomProjection ? static_cast<int64_t>(UnitVector) : scale;
}

// Moves along the freedom vector so the projected position changes by
// distance. A freedom vector nearly perpendicular to projection is treated as
// parallel instead of amplifying the move.
kstd::error_code Executor::move_point(Zone &zone, uint32_t index, int64_t distance) {
  const auto &geometry = state_->geometry;
  const int64_t scale = movement_scale();
  Point point = zone.current.data()[index];
  uint8_t touched = zone.touched.data()[index];
  const int64_t dx = geometry.freedom.x ? divide_nearest(distance * geometry.freedom.x, scale) : 0;
  const int64_t dy = geometry.freedom.y ? divide_nearest(distance * geometry.freedom.y, scale) : 0;
  if (point.x + dx < INT32_MIN || point.x + dx > INT32_MAX ||
      point.y + dy < INT32_MIN || point.y + dy > INT32_MAX)
    return Error::ArithmeticOverflow;
  if (geometry.freedom.x) { point.x = static_cast<int32_t>(point.x + dx); touched |= TouchedX; }
  if (geometry.freedom.y) { point.y = static_cast<int32_t>(point.y + dy); touched |= TouchedY; }
  zone.current.data()[index] = point;
  zone.touched.data()[index] = touched;
  return Error::None;
}

// SHPIX: moves each loop point by amount along the freedom vector, in zp2.
// The loop count is consumed and reset even when no point operand is needed.
kstd::error_code Executor::shift_pixels() {
  if (!state_) return Error::InvalidState;
  auto &geometry = state_->geometry;
  const uint32_t loop = geometry.loop;
  if (!depth_) return Error::StackUnderflow;
  if (depth_ - 1 < loop) return Error::StackUnderflow;
  const int64_t amount = operand(depth_ - 1);
  const int64_t dx = divide_nearest(amount * geometry.freedom.x, UnitVector);
  const int64_t dy = divide_nearest(amount * geometry.freedom.y, UnitVector);
  --depth_;
  for (uint32_t i = 0; i < loop; ++i) {
    if (!charge(1)) return Error::WorkLimit;
    Zone *zone = nullptr;
    const int32_t index = operand(--depth_);
    if (auto error = locate(geometry.zone[Zp2], index, zone); error) return error;
    if (auto error = shift_point(*zone, static_cast<size_t>(index), dx, dy, true); error) return error;
  }
  geometry.loop = DefaultLoop;
  return Error::None;
}

// ALIGNRP: removes each loop point's projected distance from rp0.
kstd::error_code Executor::align_to_reference() {
  if (!state_) return Error::InvalidState;
  auto &geometry = state_->geometry;
  const uint32_t loop = geometry.loop;
  if (depth_ < loop) return Error::StackUnderflow;
  Zone *reference = nullptr;
  if (auto error = locate(geometry.zone[Zp0], static_cast<int32_t>(geometry.reference[Rp0]), reference);
      error) return error;
  for (uint32_t i = 0; i < loop; ++i) {
    if (!charge(1)) return Error::WorkLimit;
    Zone *zone = nullptr;
    const int32_t index = operand(--depth_);
    if (auto error = locate(geometry.zone[Zp1], index, zone); error) return error;
    const Point a = zone->current.data()[index];
    const Point b = reference->current.data()[geometry.reference[Rp0]];
    if (auto error = move_point(*zone, static_cast<uint32_t>(index),
                                -project({.x = a.x - b.x, .y = a.y - b.y}));
        error) return error;
  }
  geometry.loop = DefaultLoop;
  return Error::None;
}

// IUP: positions untouched points between touched neighbors on one axis,
// using scaled original coordinates for ratios instead of unscaled outlines.
kstd::error_code Executor::interpolate_untouched(uint8_t op) {
  if (!state_) return Error::InvalidState;
  auto &zone = state_->glyph;
  const bool horizontal = op & opcode::FlagOdd;
  const uint8_t flag = horizontal ? TouchedX : TouchedY;
  const auto axis = [horizontal](Point &point) -> int32_t & { return horizontal ? point.x : point.y; };
  const auto contours = state_->glyphContours;
  const size_t count = zone.current.size();
  uint32_t start = 0;
  bool overflow = false;
  const auto run = [&](uint32_t first, uint32_t last, uint32_t a, uint32_t b) {
    if (first > last) return;
    int32_t orgA = axis(zone.original.data()[a]), orgB = axis(zone.original.data()[b]);
    int32_t curA = axis(zone.current.data()[a]), curB = axis(zone.current.data()[b]);
    if (orgA > orgB) {
      const int32_t o = orgA; orgA = orgB; orgB = o;
      const int32_t c = curA; curA = curB; curB = c;
    }
    const int64_t deltaA = static_cast<int64_t>(curA) - orgA, deltaB = static_cast<int64_t>(curB) - orgB;
    for (uint32_t i = first; i <= last; ++i) {
      const int32_t original = axis(zone.original.data()[i]);
      int64_t value;
      if (original <= orgA) value = original + deltaA;
      else if (original >= orgB) value = original + deltaB;
      else if (curA == curB || orgA == orgB) value = curA;
      else value = curA + divide_nearest((static_cast<int64_t>(original) - orgA) * (static_cast<int64_t>(curB) - curA),
                                         static_cast<int64_t>(orgB) - orgA);
      if (value < INT32_MIN || value > INT32_MAX) overflow = true;
      else axis(zone.current.data()[i]) = static_cast<int32_t>(value);
    }
  };
  for (const uint32_t end : contours) {
    if (end < start || end >= count) return Error::InvalidPoint;
    if (!charge(end - start + 1)) return Error::WorkLimit;
    uint32_t firstTouched = start;
    while (firstTouched <= end && !(zone.touched.data()[firstTouched] & flag)) ++firstTouched;
    if (firstTouched <= end) {
      uint32_t lastTouched = firstTouched;
      for (uint32_t point = firstTouched + 1; point <= end; ++point) {
        if (!(zone.touched.data()[point] & flag)) continue;
        run(lastTouched + 1, point - 1, lastTouched, point);
        lastTouched = point;
      }
      if (lastTouched == firstTouched) {
        const int64_t delta = static_cast<int64_t>(axis(zone.current.data()[firstTouched])) -
                              axis(zone.original.data()[firstTouched]);
        for (uint32_t i = start; i <= end; ++i) {
          if (i == firstTouched) continue;
          const int64_t value = axis(zone.original.data()[i]) + delta;
          if (value < INT32_MIN || value > INT32_MAX) overflow = true;
          else axis(zone.current.data()[i]) = static_cast<int32_t>(value);
        }
      } else {
        run(lastTouched + 1, end, lastTouched, firstTouched);
        if (firstTouched > start) run(start, firstTouched - 1, lastTouched, firstTouched);
      }
    }
    if (overflow) return Error::ArithmeticOverflow;
    start = end + 1;
  }
  return Error::None;
}

kstd::error_code Executor::point_op(uint8_t op) {
  if (!state_) return Error::InvalidState;
  auto &geometry = state_->geometry;
  const uint32_t needed = (op == opcode::MDAP0 || op == opcode::MDAP1 || op == opcode::GC0 || op == opcode::GC1) ? 1 : 2;
  if (depth_ < needed) return Error::StackUnderflow;
  const auto rounded = [&](int64_t value, int64_t &out) -> kstd::error_code {
    if (value < INT32_MIN || value > INT32_MAX) return Error::ArithmeticOverflow;
    out = rounded_distance(static_cast<int32_t>(value), state_->parameters);
    return Error::None;
  };
  Zone *zone = nullptr;
  if (op == opcode::MDAP0 || op == opcode::MDAP1) {
    const int32_t index = operand(depth_ - 1);
    if (auto error = locate(geometry.zone[Zp0], index, zone); error) return error;
    int64_t delta = 0;
    if (op == opcode::MDAP1) {
      const int64_t current = project(zone->current.data()[index]);
      int64_t target;
      if (auto error = rounded(current, target); error) return error;
      delta = target - current;
    }
    if (auto error = move_point(*zone, static_cast<uint32_t>(index), delta); error) return error;
    geometry.reference[Rp0] = geometry.reference[Rp1] = static_cast<uint32_t>(index);
    --depth_;
    return Error::None;
  }
  if (op == opcode::MIAP0 || op == opcode::MIAP1) {
    const int32_t cvtIndex = operand(depth_ - 1);
    const int32_t index = operand(depth_ - 2);
    if (cvtIndex < 0 || static_cast<uint32_t>(cvtIndex) >= state_->cvt.size()) return Error::InvalidCvtIndex;
    if (auto error = locate(geometry.zone[Zp0], index, zone); error) return error;
    int64_t distance = state_->cvt.data()[cvtIndex];
    if (geometry.zone[Zp0] == TwilightZone) {
      // Twilight points are created at the CVT distance along the freedom vector.
      const Point placed{.x = static_cast<int32_t>(divide_nearest(distance * geometry.freedom.x, UnitVector)),
                         .y = static_cast<int32_t>(divide_nearest(distance * geometry.freedom.y, UnitVector))};
      zone->original.data()[index] = placed;
      zone->current.data()[index] = placed;
    }
    const int64_t current = project(zone->current.data()[index]);
    if (op == opcode::MIAP1) {
      const int64_t gap = distance > current ? distance - current : current - distance;
      if (gap > state_->parameters.cvtCutIn) distance = current;
      if (auto error = rounded(distance, distance); error) return error;
    }
    if (auto error = move_point(*zone, static_cast<uint32_t>(index), distance - current); error)
      return error;
    geometry.reference[Rp0] = geometry.reference[Rp1] = static_cast<uint32_t>(index);
    depth_ -= 2;
    return Error::None;
  }
  if (op == opcode::GC0 || op == opcode::GC1) {
    const int32_t index = operand(depth_ - 1);
    if (auto error = locate(geometry.zone[Zp2], index, zone); error) return error;
    --depth_;
    return push(op == opcode::GC1 ? dot(zone->original.data()[index], geometry.dualProjection)
                           : project(zone->current.data()[index]));
  }
  if (op == opcode::SCFS) {
    const int32_t value = operand(depth_ - 1);
    const int32_t index = operand(depth_ - 2);
    if (auto error = locate(geometry.zone[Zp2], index, zone); error) return error;
    if (auto error = move_point(*zone, static_cast<uint32_t>(index),
                                static_cast<int64_t>(value) - project(zone->current.data()[index]));
        error) return error;
    if (geometry.zone[Zp2] == TwilightZone) zone->original.data()[index] = zone->current.data()[index];
    depth_ -= 2;
    return Error::None;
  }
  Zone *second = nullptr;
  const int32_t right = operand(depth_ - 1);
  const int32_t left = operand(depth_ - 2);
  if (auto error = locate(geometry.zone[Zp0], left, zone); error) return error;
  if (auto error = locate(geometry.zone[Zp1], right, second); error) return error;
  const bool grid = op == opcode::MD0;
  const Point a = (grid ? zone->current : zone->original).data()[left];
  const Point b = (grid ? second->current : second->original).data()[right];
  const Point difference{.x = static_cast<int32_t>(static_cast<int64_t>(a.x) - b.x), .y = static_cast<int32_t>(static_cast<int64_t>(a.y) - b.y)};
  if (static_cast<int64_t>(a.x) - b.x != difference.x || static_cast<int64_t>(a.y) - b.y != difference.y)
    return Error::ArithmeticOverflow;
  depth_ -= 2;
  return push(grid ? project(difference) : dot(difference, geometry.dualProjection));
}

// Displacement of a reference point from its original position, projected and
// re-expressed along the freedom vector. Odd opcodes use rp1 in zp0; even use rp2 in zp1.
kstd::error_code Executor::displacement(uint8_t op, int64_t &dx, int64_t &dy, uint32_t &zoneId, uint32_t &refp) {
  auto &geometry = state_->geometry;
  const bool first = op & opcode::FlagOdd;
  zoneId = geometry.zone[first ? Zp0 : Zp1];
  refp = geometry.reference[first ? Rp1 : Rp2];
  Zone *base = nullptr;
  if (auto error = locate(zoneId, static_cast<int32_t>(refp), base); error) return error;
  const Point current = base->current.data()[refp], original = base->original.data()[refp];
  const int64_t distance = project({.x = current.x - original.x, .y = current.y - original.y});
  const int64_t scale = movement_scale();
  dx = divide_nearest(distance * geometry.freedom.x, scale);
  dy = divide_nearest(distance * geometry.freedom.y, scale);
  return Error::None;
}

// SHP: applies the displacement of rp2 (zp1) or rp1 (zp0) to each loop point in zp2.
kstd::error_code Executor::shift_by_reference(uint8_t op) {
  if (!state_) return Error::InvalidState;
  auto &geometry = state_->geometry;
  const uint32_t loop = geometry.loop;
  if (depth_ < loop) return Error::StackUnderflow;
  int64_t dx, dy;
  uint32_t zoneId, refp;
  if (auto error = displacement(op, dx, dy, zoneId, refp); error) return error;
  for (uint32_t i = 0; i < loop; ++i) {
    if (!charge(1)) return Error::WorkLimit;
    Zone *zone = nullptr;
    const int32_t index = operand(--depth_);
    if (auto error = locate(geometry.zone[Zp2], index, zone); error) return error;
    if (auto error = shift_point(*zone, static_cast<size_t>(index), dx, dy, true); error) return error;
  }
  geometry.loop = DefaultLoop;
  return Error::None;
}

// IP: scales each loop point's original distance from rp1 by the current/original span of rp1..rp2.
kstd::error_code Executor::interpolate_points() {
  if (!state_) return Error::InvalidState;
  auto &geometry = state_->geometry;
  const uint32_t loop = geometry.loop;
  if (depth_ < loop) return Error::StackUnderflow;
  Zone *zone0 = nullptr, *zone1 = nullptr;
  const int32_t rp1 = static_cast<int32_t>(geometry.reference[Rp1]), rp2 = static_cast<int32_t>(geometry.reference[Rp2]);
  if (auto error = locate(geometry.zone[Zp0], rp1, zone0); error) return error;
  if (auto error = locate(geometry.zone[Zp1], rp2, zone1); error) return error;
  const Point originalBase = zone0->original.data()[rp1], currentBase = zone0->current.data()[rp1];
  const Point originalEnd = zone1->original.data()[rp2], currentEnd = zone1->current.data()[rp2];
  const int64_t oldRange = dot({.x = originalEnd.x - originalBase.x, .y = originalEnd.y - originalBase.y},
                               geometry.dualProjection);
  const int64_t newRange = project({.x = currentEnd.x - currentBase.x, .y = currentEnd.y - currentBase.y});
  for (uint32_t i = 0; i < loop; ++i) {
    if (!charge(1)) return Error::WorkLimit;
    Zone *zone = nullptr;
    const int32_t index = operand(--depth_);
    if (auto error = locate(geometry.zone[Zp2], index, zone); error) return error;
    const Point original = zone->original.data()[index], current = zone->current.data()[index];
    const int64_t originalDistance = dot({.x = original.x - originalBase.x, .y = original.y - originalBase.y},
                                         geometry.dualProjection);
    const int64_t currentDistance = project({.x = current.x - currentBase.x, .y = current.y - currentBase.y});
    int64_t target = 0;
    if (originalDistance) target = oldRange ? divide_nearest(originalDistance * newRange, oldRange)
                                            : originalDistance;
    if (auto error = move_point(*zone, static_cast<uint32_t>(index), target - currentDistance);
        error) return error;
  }
  geometry.loop = DefaultLoop;
  return Error::None;
}

// MSIRP, MDRP and MIRP: move a zp1 point relative to rp0 and update the reference points.
kstd::error_code Executor::reference_op(uint8_t op) {
  if (!state_) return Error::InvalidState;
  auto &geometry = state_->geometry;
  const auto &parameters = state_->parameters;
  const bool msirp = op == opcode::MSIRP0 || op == opcode::MSIRP1;
  const bool mirp = op >= opcode::MIRP_FIRST;
  const uint32_t needed = (msirp || mirp) ? 2 : 1;
  if (depth_ < needed) return Error::StackUnderflow;
  const int32_t index = operand(depth_ - needed);
  Zone *zone = nullptr, *base = nullptr;
  if (auto error = locate(geometry.zone[Zp1], index, zone); error) return error;
  const int32_t rp0 = static_cast<int32_t>(geometry.reference[Rp0]);
  if (auto error = locate(geometry.zone[Zp0], rp0, base); error) return error;
  const Point baseOriginal = base->original.data()[rp0];
  int64_t operandValue = needed == 2 ? operand(depth_ - 1) : 0;
  int64_t cvtDistance = 0;
  if (mirp) {
    if (operandValue < -1 || operandValue >= static_cast<int64_t>(state_->cvt.size())) return Error::InvalidCvtIndex;
    if (operandValue >= 0) cvtDistance = state_->cvt.data()[operandValue];
    const int64_t gap = cvtDistance - parameters.singleWidth;
    if ((gap < 0 ? -gap : gap) < parameters.singleWidthCutIn)
      cvtDistance = cvtDistance >= 0 ? parameters.singleWidth : -parameters.singleWidth;
  }
  if (geometry.zone[Zp1] == TwilightZone && (msirp || mirp)) {
    // Twilight points are placed relative to rp0 along the freedom vector.
    int64_t scale = UnitVector, placed = cvtDistance;
    if (msirp) {
      placed = operandValue;
      scale = movement_scale();
    }
    Point point = baseOriginal;
    point.x = static_cast<int32_t>(point.x + divide_nearest(placed * geometry.freedom.x, scale));
    point.y = static_cast<int32_t>(point.y + divide_nearest(placed * geometry.freedom.y, scale));
    zone->original.data()[index] = point;
    zone->current.data()[index] = point;
  }
  const Point original = zone->original.data()[index];
  const int64_t originalDistance = dot({.x = original.x - baseOriginal.x, .y = original.y - baseOriginal.y},
                                       geometry.dualProjection);
  const Point baseCurrent = base->current.data()[rp0], current = zone->current.data()[index];
  const int64_t currentDistance = project({.x = current.x - baseCurrent.x, .y = current.y - baseCurrent.y});
  int64_t distance;
  if (msirp) {
    distance = operandValue;
  } else if (mirp) {
    if (parameters.autoFlip && (originalDistance < 0) != (cvtDistance < 0)) cvtDistance = -cvtDistance;
    if (op & opcode::FlagRound) {
      if (geometry.zone[Zp0] == geometry.zone[Zp1]) {
        const int64_t gap = cvtDistance - originalDistance;
        if ((gap < 0 ? -gap : gap) > parameters.cvtCutIn) cvtDistance = originalDistance;
      }
      if (cvtDistance < INT32_MIN || cvtDistance > INT32_MAX) return Error::ArithmeticOverflow;
      distance = rounded_distance(static_cast<int32_t>(cvtDistance), parameters);
    } else {
      distance = cvtDistance;
    }
  } else {
    int64_t measured = originalDistance;
    if (parameters.singleWidthCutIn > 0 &&
        measured < static_cast<int64_t>(parameters.singleWidth) + parameters.singleWidthCutIn &&
        measured > static_cast<int64_t>(parameters.singleWidth) - parameters.singleWidthCutIn)
      measured = measured >= 0 ? parameters.singleWidth : -parameters.singleWidth;
    if (op & opcode::FlagRound) {
      if (measured < INT32_MIN || measured > INT32_MAX) return Error::ArithmeticOverflow;
      distance = rounded_distance(static_cast<int32_t>(measured), parameters);
    } else {
      distance = measured;
    }
  }
  if (!msirp && (op & opcode::FlagMinimumDistance)) {
    const int64_t minimum = parameters.minimumDistance;
    if (originalDistance >= 0) { if (distance < minimum) distance = minimum; }
    else if (distance > -minimum) distance = -minimum;
  }
  if (auto error = move_point(*zone, static_cast<uint32_t>(index), distance - currentDistance);
      error) return error;
  depth_ -= needed;
  geometry.reference[Rp1] = geometry.reference[Rp0];
  geometry.reference[Rp2] = static_cast<uint32_t>(index);
  if (msirp ? (op & opcode::FlagOdd) : (op & opcode::FlagSetReference0)) geometry.reference[Rp0] = static_cast<uint32_t>(index);
  return Error::None;
}
} // namespace kernel::font::vm::detail
