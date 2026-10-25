#pragma once

#include "vm_constants.hpp"

#include <span.hpp>
#include <system_error.hpp>
#include <undos/font/resource.hpp>

namespace kernel::font::vm {

enum class Phase { Font, Prep, Glyph };

enum class RoundMode { Grid, HalfGrid, DoubleGrid, Down, Up, Off, Super, Super45 };

// Persistent graphics parameters. Point movement and scan conversion are pending.
struct Parameters {
  RoundMode round{RoundMode::Grid};
  int32_t cvtCutIn{DefaultCvtCutIn};
  int32_t singleWidthCutIn{0};
  int32_t singleWidth{0};
  int32_t minimumDistance{DefaultMinimumDistance};
  uint32_t deltaBase{DefaultDeltaBase};
  uint32_t deltaShift{DefaultDeltaShift};
  bool autoFlip{true};
  uint32_t scanControl{0};
  uint32_t scanType{0};
  // F26Dot6 super-rounding settings used by RoundMode::Super and Super45.
  int32_t roundPeriod{Pixel};
  int32_t roundPhase{0};
  int32_t roundThreshold{HalfPixel};
};

struct Vector {
  int32_t x{UnitVector};
  int32_t y{0};
};

// These values always start at rasterizer defaults at top-level entry.
struct Geometry {
  Vector projection{};
  Vector dualProjection{};
  Vector freedom{};
  uint32_t reference[ReferencePointCount]{0, 0, 0};
  uint32_t zone[ZonePointerCount]{GlyphZone, GlyphZone, GlyphZone};
  uint32_t loop{1};
};

// Error codes are values of FontVmCategory. None is zero, so a successful
// kstd::error_code is false.
enum class Error : int {
  None = 0, InvalidWorkspace, TruncatedInstruction, InvalidConditional,
  StackUnderflow, StackOverflow, InvalidStackIndex, ArithmeticOverflow,
  DivisionByZero, InvalidJump, WorkLimit, UnsupportedInstruction,
  InvalidState, InvalidStorageIndex, UninitializedStorage, InvalidCvtIndex,
  InvalidDefinition, DefinitionInGlyph, InvalidFunction, InvalidRepeatCount, CallLimit,
  InvalidParameter, InvalidPoint
};

// The category outlives every error_code that refers to it.
const kstd::error_category &font_vm_category() noexcept;

inline kstd::error_code make_error_code(Error error) noexcept {
  if (error == Error::None) return {};
  return kstd::error_code(static_cast<int>(error), font_vm_category());
}

struct Point {
  int32_t x{0};
  int32_t y{0};
};

// Zone points are scaled F26Dot6. The three spans must have equal sizes, and
// the optional onCurve span, used only by FLIPPT and FLIPRG*, must be empty or
// match. Bit 0 of touched marks x movement and bit 1 marks y movement. Original points only
// change through twilight setup and are never changed by hinting moves.
struct Zone {
  kstd::span<Point> original{};
  kstd::span<Point> current{};
  kstd::span<uint8_t> touched{};
  kstd::span<uint8_t> onCurve{};
};

struct Function {
  undos::font::Bytes body{};
  bool defined{false};
};

// An IDEF body. The interpreter runs it, like a function, for an opcode that
// has no built-in meaning.
struct InstructionDef {
  uint8_t opcode{0};
  undos::font::Bytes body{};
  bool defined{false};
};

// Kernel-owned realization workspace. All buffers must be disjoint from each
// other and execution buffers. Storage is maxp-bounded; CVT holds scaled 26.6.
// Only initialize() may mark a fresh/failed state ready. Do not change its
// buffers, size or scale between initialization and execution.
struct State {
  kstd::span<int32_t> storage;
  kstd::span<uint8_t> storageWritten;
  kstd::span<int32_t> cvt;
  uint32_t ppem{0};
  uint32_t unitsPerEm{0};
  bool ready{false};
  // Sized from maxFunctionDefs. Bodies borrow immutable kernel-owned program
  // bytes, which must outlive this state. Never retain user upload pointers.
  kstd::span<Function> functions{};
  // Sized from maxInstructionDefs. Same ownership rules as functions.
  kstd::span<InstructionDef> instructionDefs{};
  Parameters parameters{};
  Parameters prepared{};
  Geometry geometry{};
  // Zone 0 is zeroed by initialize() and persists across glyphs. Zone 1 is
  // loaded by the caller before each Glyph run, including any phantom points.
  Zone twilight{};
  Zone glyph{};
  // Inclusive contour end indices for zone 1, strictly increasing and below
  // the glyph point count. Phantom points follow the last contour.
  kstd::span<const uint32_t> glyphContours{};
  uint32_t instructionControl{0};
  uint32_t preparedInstructionControl{0};
};

// Initialize from the normalized little-endian signed int32 CVT section.
// Resets storage/write flags and scales CVT once; an error leaves state invalid.
kstd::error_code initialize(State &state, undos::font::Bytes sourceCvt,
                 uint32_t ppem, uint32_t unitsPerEm);

struct Result {
  kstd::error_code error;
  uint32_t offset;
  uint32_t opcode;
  uint32_t stackSize;
  uint32_t instructions;
  uint32_t work;
  // Identifies the active top-level program or function body on failure.
  undos::font::Bytes program{};
  bool glyphInhibited{false};
};

// Allocation-free numeric/control-flow core, not yet a glyph interpreter.
// Stack starts empty. Supply maxp-bounded stack storage and program.size+1
// boundary bytes in disjoint, non-aliasing buffers. Stateful calls need enough
// boundary space for the largest retained function too. Inputs remain stable.
// On error discard all stack contents; offset/opcode identify the failure.
// Work includes decoding, skipped bytes and stack moves, not just opcodes.
Result execute(undos::font::Bytes program, kstd::span<int32_t> stack,
               kstd::span<uint8_t> boundaries, uint32_t workLimit);

// Retains storage/CVT writes between successful runs; stack still starts empty.
// Any execution error invalidates state: reinitialize before reuse. No rollback.
// Definitions are allowed only in Font/Prep, including through nested calls.
// Prep begins with defaults and saves them on success, honoring INSTCTRL 2.
// Glyph restores the saved parameters/control, resets geometry, and honors
// INSTCTRL 1 (reported as glyphInhibited). Local changes do not leak.
Result execute(undos::font::Bytes program, kstd::span<int32_t> stack,
               kstd::span<uint8_t> boundaries, State &state, uint32_t workLimit,
               Phase phase = Phase::Glyph);

} // namespace kernel::font::vm

namespace kstd {
template <>
struct is_error_code_enum<kernel::font::vm::Error> : true_type {};
} // namespace kstd
