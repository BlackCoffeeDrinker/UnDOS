#include "vm_internal.hpp"

namespace kernel::font::vm::detail {
bool decode(Bytes program, uint32_t at, Instruction &out) {
  const uint8_t op = program.data[at];
  out = {.size = 1, .count = 0, .data = at + 1, .words = false};
  if (op == opcode::NPUSHB || op == opcode::NPUSHW) {
    if (program.size - at < PushHeaderSize) return false;
    out.count = program.data[at + 1];
    out.words = op == opcode::NPUSHW;
    out.data = at + PushHeaderSize;
  } else if (op >= opcode::PUSHB_FIRST && op <= opcode::PUSH_LAST) {
    out.count = (op & opcode::PushCountMask) + 1;
    out.words = op >= opcode::PUSHW_FIRST;
  }
  const uint32_t bytes = out.count * (out.words ? WordSize : ByteSize);
  if (bytes > program.size - out.data) return false;
  out.size = out.data - at + bytes;
  return true;
}

int64_t isqrt(uint64_t value) {
  uint64_t root = 0, bit = IsqrtStartBit;
  while (bit > value) bit >>= 2;
  while (bit) {
    if (value >= root + bit) { value -= root + bit; root = (root >> 1) + bit; }
    else root >>= 1;
    bit >>= 2;
  }
  return static_cast<int64_t>(root);
}

// Super rounding follows the TrueType definition with no engine compensation.
int64_t rounded_distance(int32_t value, const Parameters &parameters) {
  const int64_t magnitude = value < 0 ? -static_cast<int64_t>(value) : value;
  int64_t rounded;
  switch (parameters.round) {
    case RoundMode::Grid: rounded = (magnitude + HalfPixel) / Pixel * Pixel; break;
    case RoundMode::HalfGrid: rounded = magnitude / Pixel * Pixel + HalfPixel; break;
    case RoundMode::DoubleGrid: rounded = (magnitude + QuarterPixel) / HalfPixel * HalfPixel; break;
    case RoundMode::Down: rounded = magnitude / Pixel * Pixel; break;
    case RoundMode::Up: rounded = (magnitude + Pixel - 1) / Pixel * Pixel; break;
    case RoundMode::Off: rounded = magnitude; break;
    case RoundMode::Super:
    case RoundMode::Super45: {
      const int64_t period = parameters.roundPeriod, phase = parameters.roundPhase;
      if (period <= 0) return INT64_MAX;
      const int64_t offset = magnitude + parameters.roundThreshold - phase;
      int64_t steps = offset / period;
      // Super45 truncates like the reference; Super masks, which floors.
      if (parameters.round == RoundMode::Super && offset % period < 0) --steps;
      const int64_t grid = steps * period;
      if (value >= 0) { const int64_t result = grid + phase; return result < 0 ? phase : result; }
      const int64_t result = -grid - phase;
      return result > 0 ? -phase : result;
    }
    default: return INT64_MAX;
  }
  return value < 0 ? -rounded : rounded;
}

int64_t divide_nearest(int64_t numerator, int64_t denominator) {
  const bool negative = (numerator < 0) != (denominator < 0);
  const uint64_t n = numerator < 0 ? -static_cast<uint64_t>(numerator) : static_cast<uint64_t>(numerator);
  const uint64_t d = denominator < 0 ? -static_cast<uint64_t>(denominator) : static_cast<uint64_t>(denominator);
  const auto quotient = static_cast<int64_t>((n * 2 + d) / (d * 2));
  return negative ? -quotient : quotient;
}

kstd::error_code Executor::prepare() {
  bool seenElse[MaxNesting]{};
  uint32_t nesting = 0;
  uint32_t outerNesting = 0;
  bool definition = false;
  for (uint32_t i = 0; i <= program_.size; ++i) boundaries_.data()[i] = 0;
  while (at_ < program_.size) {
    Instruction instruction{};
    if (!decode(program_, at_, instruction)) return Error::TruncatedInstruction;
    if (!charge(instruction.size)) return Error::WorkLimit;
    boundaries_.data()[at_] = 1;
    const uint8_t op = program_.data[at_];
    if (op == opcode::FDEF || op == opcode::IDEF) {
      if (definition) return Error::InvalidDefinition;
      definition = true;
      outerNesting = nesting;
    } else if (op == opcode::ENDF) {
      if (!definition || nesting != outerNesting) return Error::InvalidDefinition;
      definition = false;
    } else if (op == opcode::IF) {
      if (nesting == MaxNesting) return Error::InvalidConditional;
      seenElse[nesting++] = false;
    } else if (op == opcode::ELSE) {
      if (!nesting || (definition && nesting == outerNesting) || seenElse[nesting - 1])
        return Error::InvalidConditional;
      seenElse[nesting - 1] = true;
    } else if (op == opcode::EIF) {
      if (!nesting || (definition && nesting == outerNesting)) return Error::InvalidConditional;
      --nesting;
    }
    at_ += instruction.size;
  }
  boundaries_.data()[at_] = 1;
  if (definition) return Error::InvalidDefinition;
  return nesting ? Error::InvalidConditional : Error::None;
}

kstd::error_code Executor::enter(Bytes body, uint32_t count, uint32_t next) {
  if (callDepth_ == MaxCalls) return Error::CallLimit;
  if (body.size > undos::font::MaxProgramBytes || (body.size && !body.data))
    return Error::InvalidFunction;
  if (boundaries_.size() < body.size + 1) return Error::InvalidWorkspace;
  calls_[callDepth_++] = {.caller = program_, .body = body, .next = next, .remaining = count};
  program_ = body;
  at_ = 0;
  if (const auto error = prepare(); error) return error;
  at_ = 0;
  return Error::None;
}

kstd::error_code Executor::return_call() {
  auto &frame = calls_[callDepth_ - 1];
  // Charge even an empty function so a large LOOPCALL cannot evade the budget.
  if (!charge(1)) return Error::WorkLimit;
  const uint32_t next = --frame.remaining ? 0 : frame.next;
  program_ = frame.remaining ? frame.body : frame.caller;
  if (!frame.remaining) --callDepth_;
  at_ = 0;
  if (const auto error = prepare(); error) return error;
  at_ = next;
  return Error::None;
}

kstd::error_code Executor::skip(uint32_t &next, bool allowElse) {
  uint32_t nesting = 0;
  while (next < program_.size) {
    Instruction instruction{};
    decode(program_, next, instruction); // Entire stream was decoded by prepare.
    if (!charge(instruction.size)) return Error::WorkLimit;
    const uint8_t op = program_.data[next];
    next += instruction.size;
    if (op == opcode::IF) ++nesting;
    else if (op == opcode::EIF) {
      if (!nesting) return Error::None;
      --nesting;
    } else if (op == opcode::ELSE && !nesting && allowElse) return Error::None;
  }
  return Error::InvalidConditional;
}

kstd::error_code Executor::step(const Instruction &instruction, uint32_t &next) {
  const auto op = static_cast<opcode::Op>(program_.data[at_]);
  switch (op) {
    case opcode::NPUSHB: case opcode::NPUSHW: return push_data(instruction);
    case opcode::CLEAR: depth_ = 0; return Error::None;
    case opcode::DEPTH: return push(depth_);
    case opcode::EIF: return Error::None;
    case opcode::ELSE: return skip(next, false);
    case opcode::GETINFO: return get_info();
    case opcode::SPVTL0: case opcode::SPVTL1: case opcode::SFVTL0: case opcode::SFVTL1:
    case opcode::SPVFS: case opcode::SFVFS: case opcode::SDPVTL0: case opcode::SDPVTL1:
      return vector_op(op);
    case opcode::SROUND: case opcode::S45ROUND: return set_super_round(op);
    case opcode::SHC0: case opcode::SHC1: case opcode::SHZ0: case opcode::SHZ1:
      return shift_contour_or_zone(op);
    case opcode::ISECT: return intersect();
    case opcode::ALIGNPTS: return align_points();
    case opcode::UTP: return untouch_point();
    case opcode::DELTAP1: case opcode::DELTAP2: case opcode::DELTAP3: return delta_points(op);
    case opcode::DELTAC1: case opcode::DELTAC2: case opcode::DELTAC3: return delta_cvt(op);
    case opcode::FLIPPT: case opcode::FLIPRGON: case opcode::FLIPRGOFF: return flip_points(op);
    case opcode::SHPIX: return shift_pixels();
    case opcode::SHP0: case opcode::SHP1: return shift_by_reference(op);
    case opcode::IP: return interpolate_points();
    case opcode::MSIRP0: case opcode::MSIRP1: return reference_op(op);
    case opcode::ALIGNRP: return align_to_reference();
    case opcode::IUP0: case opcode::IUP1: return interpolate_untouched(op);
    case opcode::MDAP0: case opcode::MDAP1: case opcode::MIAP0: case opcode::MIAP1:
    case opcode::GC0: case opcode::GC1: case opcode::SCFS: case opcode::MD0: case opcode::MD1:
      return point_op(op);
    case opcode::MPPEM: case opcode::MPS: {
      if (!state_) return Error::InvalidState;
      return push(op == opcode::MPPEM ? state_->ppem : state_->ppem * PointSizePerPpem); // 96 DPI.
    }
    case opcode::SVTCA_Y: case opcode::SVTCA_X: case opcode::SPVTCA_Y: case opcode::SPVTCA_X:
    case opcode::SFVTCA_Y: case opcode::SFVTCA_X: case opcode::GPV: case opcode::GFV:
    case opcode::SFVTPV:
      return vector_state_op(op);
    case opcode::SRP0: case opcode::SRP1: case opcode::SRP2: case opcode::SZP0: case opcode::SZP1:
    case opcode::SZP2: case opcode::SZPS: case opcode::SLOOP: case opcode::SCANCTRL:
    case opcode::SCANTYPE: case opcode::INSTCTRL:
      return parameter_op(op);
    case opcode::RTG: case opcode::RTHG: case opcode::RTDG: case opcode::ROFF: case opcode::RUTG:
    case opcode::RDTG: case opcode::FLIPON: case opcode::FLIPOFF:
      return round_mode_op(op);
    case opcode::SMD: case opcode::SCVTCI: case opcode::SSWCI: case opcode::SSW: case opcode::SDB:
    case opcode::SDS: case opcode::ODD: case opcode::EVEN:
      return value_op(op);
    case opcode::FDEF: return define_function(next);
    case opcode::IDEF: return define_instruction(next);
    case opcode::ENDF: return Error::InvalidDefinition;
    case opcode::CALL: case opcode::LOOPCALL: return call_op(op, next);
    default: break;
  }
  if (op >= opcode::PUSHB_FIRST && op <= opcode::PUSH_LAST) return push_data(instruction);
  if (op >= opcode::MDRP_FIRST) return reference_op(op);
  if (op >= opcode::ROUND_FIRST && op <= opcode::NROUND_RESERVED) return value_op(op);
  return stack_op(op, next);
}

kstd::error_code Executor::push_data(const Instruction &instruction) {
  if (instruction.count > stack_.size() - depth_) return Error::StackOverflow;
  uint32_t data = instruction.data;
  for (uint32_t i = 0; i < instruction.count; ++i) {
    int32_t value = program_.data[data++];
    if (instruction.words) {
      value = value * ByteRange + program_.data[data++];
      if (value >= WordSignBit) value -= WordRange;
    }
    operand(depth_++) = value;
  }
  return Error::None;
}

kstd::error_code Executor::get_info() {
  if (!depth_) return Error::StackUnderflow;
  // Version 35 only; every optional capability bit stays clear in this
  // monochrome, unrotated, unstretched profile.
  const auto selector = static_cast<uint32_t>(operand(depth_ - 1));
  --depth_;
  return push((selector & GetInfoVersionBit) ? InterpreterVersion : 0);
}

kstd::error_code Executor::vector_state_op(uint8_t op) {
  if (!state_) return Error::InvalidState;
  auto &geometry = state_->geometry;
  if (op <= opcode::SFVTCA_X) {
    const Vector axis = (op & opcode::FlagOdd) ? Vector{} : Vector{.x = 0, .y = UnitVector};
    if (op <= opcode::SPVTCA_X) {
      geometry.projection = axis;
      geometry.dualProjection = axis;
    }
    if (op <= opcode::SVTCA_X || op >= opcode::SFVTCA_Y) geometry.freedom = axis;
  } else if (op == opcode::SFVTPV) {
    geometry.freedom = geometry.projection;
  } else {
    if (stack_.size() - depth_ < VectorComponents) return Error::StackOverflow;
    const auto vector = op == opcode::GPV ? geometry.projection : geometry.freedom;
    operand(depth_++) = vector.x;
    operand(depth_++) = vector.y;
  }
  return Error::None;
}

kstd::error_code Executor::parameter_op(uint8_t op) {
  if (!state_) return Error::InvalidState;
  const uint32_t needed = op == opcode::INSTCTRL ? 2 : 1;
  if (depth_ < needed) return Error::StackUnderflow;
  const int32_t value = operand(depth_ - 1);
  auto &geometry = state_->geometry;
  if (op == opcode::INSTCTRL) {
    const int32_t setting = operand(depth_ - 2);
    if (value < InstructionControlSelectorMin || value > InstructionControlSelectorMax ||
        (setting != 0 && setting != (1 << (value - 1))))
      return Error::InvalidParameter;
    if (phase_ == Phase::Font) return Error::InvalidParameter;
    // Selectors 1/2 have no effect in a glyph. Selector 3 is retained only;
    // no ClearType behavior is enabled by this monochrome interpreter.
    if (phase_ == Phase::Prep || value == InstructionControlNativeClearType) {
      const uint32_t mask = 1u << static_cast<uint32_t>(value - 1);
      state_->instructionControl = (state_->instructionControl & ~mask) | static_cast<uint32_t>(setting);
    }
  } else if (op == opcode::SCANCTRL) {
    state_->parameters.scanControl = static_cast<uint32_t>(value) & Word16Mask;
  } else if (op == opcode::SCANTYPE) {
    const uint32_t type = static_cast<uint32_t>(value) & Word16Mask;
    if (type > MaxScanType) return Error::InvalidParameter;
    state_->parameters.scanType = type;
  } else {
    if (value < 0) return Error::InvalidParameter;
    if (op <= opcode::SRP2) geometry.reference[op - opcode::SRP0] = static_cast<uint32_t>(value);
    else if (op <= opcode::SZPS) {
      if (value > static_cast<int32_t>(GlyphZone)) return Error::InvalidParameter;
      if (op == opcode::SZPS) {
        for (auto &zone : geometry.zone) zone = static_cast<uint32_t>(value);
      } else geometry.zone[op - opcode::SZP0] = static_cast<uint32_t>(value);
    } else {
      if (static_cast<uint32_t>(value) > MaxWork) return Error::InvalidParameter;
      geometry.loop = static_cast<uint32_t>(value);
    }
  }
  depth_ -= needed;
  return Error::None;
}

kstd::error_code Executor::round_mode_op(uint8_t op) {
  if (!state_) return Error::InvalidState;
  auto &parameters = state_->parameters;
  switch (op) {
    case opcode::RTG: parameters.round = RoundMode::Grid; break;
    case opcode::RTHG: parameters.round = RoundMode::HalfGrid; break;
    case opcode::RTDG: parameters.round = RoundMode::DoubleGrid; break;
    case opcode::ROFF: parameters.round = RoundMode::Off; break;
    case opcode::RUTG: parameters.round = RoundMode::Up; break;
    case opcode::RDTG: parameters.round = RoundMode::Down; break;
    case opcode::FLIPON: parameters.autoFlip = true; break;
    case opcode::FLIPOFF: parameters.autoFlip = false; break;
    default: break;
  }
  return Error::None;
}

// SMD, SCVTCI, SSWCI, SSW, SDB, SDS, ODD, EVEN, ROUND and NROUND: one operand.
kstd::error_code Executor::value_op(uint8_t op) {
  if (!state_) return Error::InvalidState;
  if (!depth_) return Error::StackUnderflow;
  const int32_t value = operand(depth_ - 1);
  auto &parameters = state_->parameters;
  if (op == opcode::ODD || op == opcode::EVEN || (op >= opcode::ROUND_FIRST && op <= opcode::NROUND_RESERVED)) {
    if (op == opcode::ROUND_RESERVED || op == opcode::NROUND_RESERVED) return Error::InvalidParameter; // Reserved distance type.
    --depth_;
    // Monochrome profile uses zero engine compensation for all three colors.
    const int64_t rounded = op >= opcode::NROUND_FIRST ? static_cast<int64_t>(value) :
        rounded_distance(value, parameters);
    if (rounded < INT32_MIN || rounded > INT32_MAX) return Error::ArithmeticOverflow;
    if (op == opcode::ODD || op == opcode::EVEN)
      return push((rounded / Pixel % 2 != 0) == (op == opcode::ODD));
    return push(rounded);
  }
  if ((op == opcode::SDB || op == opcode::SDS) && value < 0) return Error::InvalidParameter;
  switch (op) {
    case opcode::SMD: parameters.minimumDistance = value; break;
    case opcode::SCVTCI: parameters.cvtCutIn = value; break;
    case opcode::SSWCI: parameters.singleWidthCutIn = value; break;
    case opcode::SSW: {
      int32_t scaled;
      if (undos::font::scale_funits(value, state_->ppem, state_->unitsPerEm, scaled) !=
          undos::font::ScaleError::None) return Error::ArithmeticOverflow;
      parameters.singleWidth = scaled;
      break;
    }
    case opcode::SDB:
      parameters.deltaBase = static_cast<uint32_t>(value);
      break;
    case opcode::SDS:
      if (value > static_cast<int32_t>(MaxDeltaShiftSetting)) return Error::InvalidParameter;
      parameters.deltaShift = static_cast<uint32_t>(value);
      break;
    default: break;
  }
  --depth_;
  return Error::None;
}

// Moves next past the ENDF that closes a definition and returns the body before it.
kstd::error_code Executor::skip_definition(uint32_t &next, Bytes &body) {
  const uint32_t begin = next;
  while (next < program_.size) {
    Instruction item{};
    decode(program_, next, item);
    if (!charge(item.size)) return Error::WorkLimit;
    const bool end = program_.data[next] == opcode::ENDF;
    if (end) body = {.data = program_.data + begin, .size = next - begin};
    next += item.size;
    if (end) return Error::None;
  }
  return Error::InvalidDefinition;
}

kstd::error_code Executor::define_function(uint32_t &next) {
  if (!state_) return Error::InvalidState;
  if (phase_ == Phase::Glyph) return Error::DefinitionInGlyph;
  if (!depth_) return Error::StackUnderflow;
  const int32_t id = operand(depth_ - 1);
  if (id < 0 || static_cast<uint32_t>(id) >= state_->functions.size()) return Error::InvalidFunction;
  Bytes body;
  if (const auto error = skip_definition(next, body); error) return error;
  state_->functions.data()[static_cast<uint32_t>(id)] = {.body = body, .defined = true};
  --depth_;
  return Error::None;
}

// IDEF defines a body for an opcode that has no built-in meaning. Built-in
// opcodes keep their meaning, so defining one is accepted but never run.
kstd::error_code Executor::define_instruction(uint32_t &next) {
  if (!state_) return Error::InvalidState;
  if (phase_ == Phase::Glyph) return Error::DefinitionInGlyph;
  if (!depth_) return Error::StackUnderflow;
  const int32_t code = operand(depth_ - 1);
  if (code < 0 || code > static_cast<int32_t>(UINT8_MAX)) return Error::InvalidParameter;
  InstructionDef *slot = nullptr;
  for (auto &definition : state_->instructionDefs) {
    if (definition.defined && definition.opcode == code) { slot = &definition; break; }
    if (!definition.defined && !slot) slot = &definition;
  }
  if (!slot) return Error::InvalidFunction;
  Bytes body;
  if (const auto error = skip_definition(next, body); error) return error;
  *slot = {.opcode = static_cast<uint8_t>(code), .body = body, .defined = true};
  --depth_;
  return Error::None;
}

kstd::error_code Executor::call_instruction_def(uint8_t op, uint32_t &next) {
  if (!state_) return Error::UnsupportedInstruction;
  for (const auto &definition : state_->instructionDefs) {
    if (!definition.defined || definition.opcode != op) continue;
    const auto error = enter(definition.body, 1, next);
    next = at_;
    return error;
  }
  return Error::UnsupportedInstruction;
}

kstd::error_code Executor::call_op(uint8_t op, uint32_t &next) {
  if (!state_) return Error::InvalidState;
  const uint32_t countOperands = op == opcode::CALL ? 1 : 2;
  if (depth_ < countOperands) return Error::StackUnderflow;
  const int32_t id = operand(depth_ - 1);
  const int32_t count = op == opcode::CALL ? 1 : operand(depth_ - 2);
  if (id < 0 || static_cast<uint32_t>(id) >= state_->functions.size() ||
      !state_->functions.data()[static_cast<uint32_t>(id)].defined)
    return Error::InvalidFunction;
  if (count < 0) return Error::InvalidRepeatCount;
  depth_ -= countOperands;
  if (!count) return Error::None;
  const auto error = enter(state_->functions.data()[static_cast<uint32_t>(id)].body, static_cast<uint32_t>(count), next);
  next = at_;
  return error;
}

// Stack, storage, CVT, arithmetic, comparison and jump instructions.
kstd::error_code Executor::stack_op(uint8_t op, uint32_t &next) {
  uint32_t needed;
  switch (op) {
    case opcode::DUP: case opcode::POP: case opcode::CINDEX: case opcode::MINDEX: case opcode::IF: case opcode::NOT:
    case opcode::ABS: case opcode::NEG: case opcode::FLOOR: case opcode::CEILING: case opcode::JMPR:
    case opcode::RS: case opcode::RCVT:
      needed = 1; break;
    case opcode::SWAP: case opcode::LT: case opcode::LTEQ: case opcode::GT: case opcode::GTEQ:
    case opcode::EQ: case opcode::NEQ: case opcode::AND: case opcode::OR: case opcode::ADD: case opcode::SUB:
    case opcode::DIV: case opcode::MUL: case opcode::JROT: case opcode::JROF: case opcode::MAX: case opcode::MIN:
    case opcode::WS: case opcode::WCVTP: case opcode::WCVTF:
      needed = 2; break;
    case opcode::ROLL: needed = 3; break;
    default: return call_instruction_def(op, next);
  }
  if (depth_ < needed) return Error::StackUnderflow;
  const int32_t top = operand(depth_ - 1);
  switch (op) {
    case opcode::WS: case opcode::RS: case opcode::WCVTP: case opcode::RCVT: case opcode::WCVTF: {
      if (!state_) return Error::InvalidState;
      const int32_t index = needed == 1 ? top : operand(depth_ - 2);
      const bool storage = op == opcode::WS || op == opcode::RS;
      auto values = storage ? state_->storage : state_->cvt;
      if (index < 0 || static_cast<uint32_t>(index) >= values.size())
        return storage ? Error::InvalidStorageIndex : Error::InvalidCvtIndex;
      const auto slot = static_cast<uint32_t>(index);
      if (needed == 1) {
        if (storage && !state_->storageWritten.data()[slot]) return Error::UninitializedStorage;
        operand(depth_ - 1) = values.data()[slot];
      } else {
        int32_t value = top;
        if (op == opcode::WCVTF &&
            undos::font::scale_funits(top, state_->ppem, state_->unitsPerEm, value) !=
                undos::font::ScaleError::None)
          return Error::ArithmeticOverflow;
        values.data()[slot] = value;
        if (storage) state_->storageWritten.data()[slot] = 1;
        depth_ -= 2;
      }
      return Error::None;
    }
    case opcode::DUP: return push(top); // DUP
    case opcode::POP: --depth_; return Error::None; // POP
    case opcode::SWAP: // SWAP
      operand(depth_ - 1) = operand(depth_ - 2);
      operand(depth_ - 2) = top;
      return Error::None;
    case opcode::CINDEX: case opcode::MINDEX: { // CINDEX / MINDEX, one-based after popping index.
      --depth_;
      if (top <= 0 || static_cast<uint32_t>(top) > depth_) return Error::InvalidStackIndex;
      const uint32_t index = depth_ - static_cast<uint32_t>(top);
      const int32_t value = operand(index);
      if (op == opcode::MINDEX) {
        if (!charge(static_cast<uint32_t>(top))) return Error::WorkLimit;
        for (uint32_t i = index; i + 1 < depth_; ++i) operand(i) = operand(i + 1);
        --depth_;
      }
      return push(value);
    }
    case opcode::ROLL: { // ROLL: [a,b,c] -> [b,c,a], bottom to top.
      const int32_t value = operand(depth_ - 3);
      operand(depth_ - 3) = operand(depth_ - 2);
      operand(depth_ - 2) = top;
      operand(depth_ - 1) = value;
      return Error::None;
    }
    case opcode::IF:
      --depth_;
      return top ? Error::None : skip(next, true);
    case opcode::JMPR: case opcode::JROT: case opcode::JROF: {
      const int32_t offset = op == opcode::JMPR ? top : operand(depth_ - 2);
      depth_ -= needed;
      if (op != opcode::JMPR && ((top != 0) != (op == opcode::JROT))) return Error::None;
      const int64_t target = static_cast<int64_t>(at_) + offset;
      if (target < 0 || target > static_cast<int64_t>(program_.size) || !boundaries_[static_cast<size_t>(target)])
        return Error::InvalidJump;
      next = static_cast<uint32_t>(target);
      return Error::None;
    }
    default: break;
  }
  if (needed == 1) {
    --depth_;
    const int64_t value = top;
    switch (op) {
      case opcode::NOT: return push(!top);
      case opcode::ABS: return push(value < 0 ? -value : value);
      case opcode::NEG: return push(-value);
      case opcode::FLOOR: return push((value / Pixel - (value % Pixel < 0)) * Pixel);
      case opcode::CEILING: return push((value / Pixel + (value % Pixel > 0)) * Pixel);
      default: return Error::UnsupportedInstruction;
    }
  }
  const int64_t left = operand(depth_ - 2), right = top;
  depth_ -= 2;
  switch (op) {
    case opcode::LT: return push(left < right);
    case opcode::LTEQ: return push(left <= right);
    case opcode::GT: return push(left > right);
    case opcode::GTEQ: return push(left >= right);
    case opcode::EQ: return push(left == right);
    case opcode::NEQ: return push(left != right);
    case opcode::AND: return push(left && right);
    case opcode::OR: return push(left || right);
    case opcode::ADD: return push(left + right);
    case opcode::SUB: return push(left - right);
    case opcode::DIV:
      if (!right) return Error::DivisionByZero;
      return push(left * Pixel / right);
    case opcode::MUL: {
      const int64_t product = left * right;
      const int64_t rounded = ((product < 0 ? -product : product) + HalfPixel) / Pixel;
      return push(product < 0 ? -rounded : rounded);
    }
    case opcode::MAX: return push(left > right ? left : right);
    case opcode::MIN: return push(left < right ? left : right);
    default: return Error::UnsupportedInstruction;
  }
}

Result Executor::run() {
  if ((program_.size && !program_.data) || program_.size > undos::font::MaxProgramBytes ||
      stack_.size() > MaxStack || (stack_.size() && !stack_.data()) ||
      boundaries_.size() < program_.size + 1 || !boundaries_.data() ||
      !limit_ || limit_ > MaxWork)
    return {.error = Error::InvalidWorkspace, .offset = 0, .opcode = 0, .stackSize = 0, .instructions = 0, .work = 0};
  if (state_ && phase_ == Phase::Glyph && (state_->instructionControl & InhibitGlyphHinting)) {
    auto inhibited = result(Error::None);
    inhibited.glyphInhibited = true;
    return inhibited;
  }
  if (const auto error = prepare(); error) return result(error);
  at_ = 0;
  while (at_ < program_.size || callDepth_) {
    if (at_ == program_.size) {
      if (const auto error = return_call(); error) return result(error);
      continue;
    }
    Instruction instruction{};
    decode(program_, at_, instruction);
    if (!charge(instruction.size)) return result(Error::WorkLimit);
    ++instructions_;
    uint32_t next = at_ + instruction.size;
    if (const auto error = step(instruction, next); error) return result(error);
    at_ = next;
  }
  return result(Error::None);
}
} // namespace kernel::font::vm::detail
