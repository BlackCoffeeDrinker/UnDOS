#pragma once
#include "vm.hpp"
#include "vm_opcodes.hpp"
#include "scaler.hpp"

namespace kernel::font::vm::detail {
using undos::font::Bytes;

struct Instruction {
  uint32_t size;
  uint32_t count;
  uint32_t data;
  bool words;
};

// Hot-path result: a bare Error, true when it is a failure. Converted to a
// kstd::error_code (category lookup) only once, in Executor::result().
class Status {
public:
  constexpr Status() = default;
  constexpr Status(Error error) : error_(error) {}
  constexpr explicit operator bool() const { return error_ != Error::None; }
  constexpr Error error() const { return error_; }
  constexpr bool operator==(Error other) const { return error_ == other; }
private:
  Error error_{Error::None};
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
  bool ppemDependent_{false};
  uint32_t limit_, at_{0}, depth_{0}, instructions_{0}, work_{0};
  int32_t &operand(uint32_t index) { return stack_.data()[index]; }
  bool charge(uint32_t amount) {
    if (amount > limit_ - work_) return false;
    work_ += amount;
    return true;
  }
  Result result(Status status) const {
    const kstd::error_code error = make_error_code(status.error());
    return {.error = error, .offset = at_, .opcode = at_ < program_.size ? program_.data[at_] : 0u,
            .stackSize = !error ? depth_ : 0u, .instructions = instructions_, .work = work_, .program = program_,
            .ppemDependent = ppemDependent_};
  }
  Status prepare();
  Status jump_target_ok(uint32_t target, bool &ok);
  Status skip(uint32_t &next, bool allowElse);
  Status step(const Instruction &instruction, uint32_t &next);
  Status push_data(const Instruction &instruction);
  Status get_info();
  Status vector_state_op(uint8_t op);
  Status parameter_op(uint8_t op);
  Status round_mode_op(uint8_t op);
  Status value_op(uint8_t op);
  Status define_function(uint32_t &next);
  Status define_instruction(uint32_t &next);
  Status skip_definition(uint32_t &next, Bytes &body);
  Status call_instruction_def(uint8_t op, uint32_t &next);
  Status call_op(uint8_t op, uint32_t &next);
  Status stack_op(uint8_t op, uint32_t &next);
  Status point_op(uint8_t op);
  Status shift_pixels();
  Status reference_op(uint8_t op);
  Status shift_by_reference(uint8_t op);
  Status interpolate_points();
  Status align_to_reference();
  Status interpolate_untouched(uint8_t op);
  Status shift_point(Zone &zone, size_t index, int64_t dx, int64_t dy, bool touch);
  Status point_vector(uint8_t op, int32_t lower, int32_t top);
  Status vector_op(uint8_t op);
  Status displacement(uint8_t op, int64_t &dx, int64_t &dy, uint32_t &zoneId, uint32_t &refp);
  Status set_super_round(uint8_t op);
  Status delta_apply(uint8_t op, bool cvt);
  Status delta_points(uint8_t op);
  Status delta_cvt(uint8_t op);
  Status shift_contour_or_zone(uint8_t op);
  Status intersect();
  Status align_points();
  Status flip_points(uint8_t op);
  Status untouch_point();
  int64_t movement_scale() const;
  Status locate(uint32_t zoneId, int32_t index, Zone *&zone) const;
  Status move_point(Zone &zone, uint32_t index, int64_t distance);
  int64_t project(Point point) const { return dot(point, state_->geometry.projection); }
  static int64_t dot(Point point, Vector vector) {
    return (static_cast<int64_t>(point.x) * vector.x + static_cast<int64_t>(point.y) * vector.y + UnitRounding) >> UnitBits;
  }
  Status enter(Bytes body, uint32_t count, uint32_t next);
  Status return_call();
  Status push(int64_t value) {
    if (value < INT32_MIN || value > INT32_MAX) return Error::ArithmeticOverflow;
    if (depth_ == stack_.size()) return Error::StackOverflow;
    operand(depth_++) = static_cast<int32_t>(value);
    return Error::None;
  }
};
} // namespace kernel::font::vm::detail
