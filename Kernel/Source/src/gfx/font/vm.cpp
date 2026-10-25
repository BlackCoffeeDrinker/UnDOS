#include "vm_internal.hpp"

#include <new.hpp>

namespace kernel::font::vm {
using detail::Executor;
using undos::font::Bytes;

namespace {
bool zone_valid(const Zone &zone) {
  const size_t count = zone.current.size();
  return count <= MaxZonePoints && zone.original.size() == count &&
      zone.touched.size() == count &&
      (zone.onCurve.empty() || (zone.onCurve.size() == count && zone.onCurve.data())) &&
      (!count || (zone.original.data() && zone.current.data() && zone.touched.data()));
}

bool valid_buffers(const State &state) {
  return state.storage.size() <= MaxStorage && state.cvt.size() <= MaxCvt &&
      state.storageWritten.size() == state.storage.size() &&
      (!state.storage.size() || (state.storage.data() && state.storageWritten.data())) &&
      (!state.cvt.size() || state.cvt.data()) &&
      state.functions.size() <= MaxFunctions &&
      (!state.functions.size() || state.functions.data()) &&
      state.instructionDefs.size() <= MaxInstructionDefs &&
      (!state.instructionDefs.size() || state.instructionDefs.data()) &&
      zone_valid(state.twilight) && zone_valid(state.glyph) &&
      (!state.glyphContours.size() || state.glyphContours.data());
}
}

kstd::error_code initialize(State &state, Bytes sourceCvt, uint32_t ppem, uint32_t unitsPerEm) {
  state.ready = false;
  if (!valid_buffers(state) || sourceCvt.size != state.cvt.size() * sizeof(int32_t) ||
      (sourceCvt.size && !sourceCvt.data))
    return Error::InvalidWorkspace;
  int32_t scaled;
  if (undos::font::scale_funits(0, ppem, unitsPerEm, scaled) != undos::font::ScaleError::None)
    return Error::InvalidState;
  for (size_t i = 0; i < state.cvt.size(); ++i) {
    if (undos::font::scale_funits(undos::font::read_i32(sourceCvt.data + i * sizeof(int32_t)),
                                ppem, unitsPerEm, scaled) != undos::font::ScaleError::None)
      return Error::ArithmeticOverflow;
    state.cvt.data()[i] = scaled;
  }
  for (size_t i = 0; i < state.storage.size(); ++i) {
    state.storage.data()[i] = 0;
    state.storageWritten.data()[i] = 0;
  }
  for (size_t i = 0; i < state.twilight.current.size(); ++i) {
    state.twilight.original.data()[i] = {};
    state.twilight.current.data()[i] = {};
    state.twilight.touched.data()[i] = 0;
  }
  state.ppem = ppem;
  state.unitsPerEm = unitsPerEm;
  state.ready = true;
  for (auto &function : state.functions) function = {};
  for (auto &definition : state.instructionDefs) definition = {};
  state.parameters = {};
  state.prepared = {};
  state.geometry = {};
  state.instructionControl = 0;
  state.preparedInstructionControl = 0;
  return Error::None;
}

Result execute(Bytes program, kstd::span<int32_t> stack,
               kstd::span<uint8_t> boundaries, uint32_t workLimit) {
  return Executor(program, stack, boundaries, workLimit).run();
}

Result execute(Bytes program, kstd::span<int32_t> stack,
               kstd::span<uint8_t> boundaries, State &state, uint32_t workLimit, Phase phase) {
  int32_t scaled;
  if (!state.ready || !valid_buffers(state) ||
      (phase != Phase::Font && phase != Phase::Prep && phase != Phase::Glyph) ||
      undos::font::scale_funits(0, state.ppem, state.unitsPerEm, scaled) != undos::font::ScaleError::None) {
    state.ready = false;
    return {.error = Error::InvalidState, .offset = 0, .opcode = 0, .stackSize = 0, .instructions = 0, .work = 0};
  }
  state.parameters = phase == Phase::Glyph ? state.prepared : Parameters{};
  state.geometry = {};
  state.instructionControl = phase == Phase::Glyph ? state.preparedInstructionControl : 0;
  const auto result = Executor(program, stack, boundaries, workLimit, &state, phase).run();
  if (result.error != Error::None) state.ready = false;
  else if (phase == Phase::Prep) {
    state.prepared = (state.instructionControl & UseDefaultParameters) ? Parameters{} : state.parameters;
    state.preparedInstructionControl = state.instructionControl;
  }
  return result;
}

namespace {
class FontVmCategory final : public kstd::error_category {
public:
  [[nodiscard]] const char *name() const noexcept override { return "font_vm_category"; }
  [[nodiscard]] kstd::string_view message(int value) const noexcept override {
    switch (static_cast<Error>(value)) {
      case Error::None: return "success";
      case Error::InvalidWorkspace: return "invalid VM workspace or limits";
      case Error::TruncatedInstruction: return "truncated instruction";
      case Error::InvalidConditional: return "invalid or excessively nested conditional";
      case Error::StackUnderflow: return "stack underflow";
      case Error::StackOverflow: return "stack limit exceeded";
      case Error::InvalidStackIndex: return "invalid stack index";
      case Error::ArithmeticOverflow: return "arithmetic overflow";
      case Error::DivisionByZero: return "division by zero";
      case Error::InvalidJump: return "jump outside program or into push data";
      case Error::WorkLimit: return "VM work limit exceeded";
      case Error::UnsupportedInstruction: return "instruction not implemented";
      case Error::InvalidState: return "VM state missing, invalid or not initialized";
      case Error::InvalidStorageIndex: return "storage index outside declared bounds";
      case Error::UninitializedStorage: return "storage read before write";
      case Error::InvalidCvtIndex: return "CVT index outside declared bounds";
      case Error::InvalidDefinition: return "malformed or misplaced function definition";
      case Error::DefinitionInGlyph: return "function definition executed in glyph phase";
      case Error::InvalidFunction: return "function id invalid, undefined or body invalid";
      case Error::InvalidRepeatCount: return "negative function repeat count";
      case Error::CallLimit: return "function call depth exceeded";
      case Error::InvalidParameter: return "invalid graphics-state parameter";
      case Error::InvalidPoint: return "point index outside selected zone";
    }
    return "unknown VM error";
  }
};
}

const kstd::error_category &font_vm_category() noexcept {
  // Error codes keep this reference; never register its destructor.
  alignas(FontVmCategory) static unsigned char storage[sizeof(FontVmCategory)];
  static const FontVmCategory &category = *new (storage) FontVmCategory;
  return category;
}

} // namespace kernel::font::vm
