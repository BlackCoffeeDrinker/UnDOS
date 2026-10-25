#pragma once
#include "vm.hpp"
#include "vm_opcodes.hpp"
#include <undos/font/scaler.hpp>

namespace kernel::font::vm::detail {
using undos::font::Bytes;

struct Instruction {
  uint32_t size;
  uint32_t count;
  uint32_t data;
  bool words;
};

bool decode(Bytes program, uint32_t at, Instruction &out);
int64_t rounded_distance(int32_t value, const Parameters &parameters);
int64_t isqrt(uint64_t value);
int64_t divide_nearest(int64_t numerator, int64_t denominator);

class Executor {
public:
  Executor(Bytes program, kstd::span<int32_t> stack,
           kstd::span<uint8_t> boundaries, uint32_t limit, State *state = nullptr,
           Phase phase = Phase::Glyph)
      : program_(program), stack_(stack), boundaries_(boundaries), state_(state),
        phase_(phase), limit_(limit) {}
  Result run();
private:
  Bytes program_;
  kstd::span<int32_t> stack_;
  kstd::span<uint8_t> boundaries_;
  State *state_;
  Phase phase_;
  struct Frame {
    Bytes caller;
    Bytes body;
    uint32_t next;
    uint32_t remaining;
  };
  Frame calls_[MaxCalls]{};
  uint32_t callDepth_{0};
  uint32_t limit_, at_{0}, depth_{0}, instructions_{0}, work_{0};
  int32_t &operand(uint32_t index) { return stack_.data()[index]; }
  bool charge(uint32_t amount) {
    if (amount > limit_ - work_) return false;
    work_ += amount;
    return true;
  }
  Result result(const kstd::error_code &error) const {
    return {.error = error, .offset = at_, .opcode = at_ < program_.size ? program_.data[at_] : 0u,
            .stackSize = !error ? depth_ : 0u, .instructions = instructions_, .work = work_, .program = program_};
  }
  kstd::error_code prepare();
  kstd::error_code skip(uint32_t &next, bool allowElse);
  kstd::error_code step(const Instruction &instruction, uint32_t &next);
  kstd::error_code push_data(const Instruction &instruction);
  kstd::error_code get_info();
  kstd::error_code vector_state_op(uint8_t op);
  kstd::error_code parameter_op(uint8_t op);
  kstd::error_code round_mode_op(uint8_t op);
  kstd::error_code value_op(uint8_t op);
  kstd::error_code define_function(uint32_t &next);
  kstd::error_code define_instruction(uint32_t &next);
  kstd::error_code skip_definition(uint32_t &next, Bytes &body);
  kstd::error_code call_instruction_def(uint8_t op, uint32_t &next);
  kstd::error_code call_op(uint8_t op, uint32_t &next);
  kstd::error_code stack_op(uint8_t op, uint32_t &next);
  kstd::error_code point_op(uint8_t op);
  kstd::error_code shift_pixels();
  kstd::error_code reference_op(uint8_t op);
  kstd::error_code shift_by_reference(uint8_t op);
  kstd::error_code interpolate_points();
  kstd::error_code align_to_reference();
  kstd::error_code interpolate_untouched(uint8_t op);
  kstd::error_code shift_point(Zone &zone, size_t index, int64_t dx, int64_t dy, bool touch);
  kstd::error_code point_vector(uint8_t op, int32_t lower, int32_t top);
  kstd::error_code vector_op(uint8_t op);
  kstd::error_code displacement(uint8_t op, int64_t &dx, int64_t &dy, uint32_t &zoneId, uint32_t &refp);
  kstd::error_code set_super_round(uint8_t op);
  kstd::error_code delta_apply(uint8_t op, bool cvt);
  kstd::error_code delta_points(uint8_t op);
  kstd::error_code delta_cvt(uint8_t op);
  kstd::error_code shift_contour_or_zone(uint8_t op);
  kstd::error_code intersect();
  kstd::error_code align_points();
  kstd::error_code flip_points(uint8_t op);
  kstd::error_code untouch_point();
  int64_t movement_scale() const;
  kstd::error_code locate(uint32_t zoneId, int32_t index, Zone *&zone) const;
  kstd::error_code move_point(Zone &zone, uint32_t index, int64_t distance);
  int64_t project(Point point) const { return dot(point, state_->geometry.projection); }
  static int64_t dot(Point point, Vector vector) {
    return (static_cast<int64_t>(point.x) * vector.x + static_cast<int64_t>(point.y) * vector.y + UnitRounding) >> UnitBits;
  }
  kstd::error_code enter(Bytes body, uint32_t count, uint32_t next);
  kstd::error_code return_call();
  kstd::error_code push(int64_t value) {
    if (value < INT32_MIN || value > INT32_MAX) return Error::ArithmeticOverflow;
    if (depth_ == stack_.size()) return Error::StackOverflow;
    operand(depth_++) = static_cast<int32_t>(value);
    return Error::None;
  }
};
} // namespace kernel::font::vm::detail
