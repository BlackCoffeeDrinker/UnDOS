#pragma once

#include <stdint.h>
#include <undos/font/resource.hpp>

namespace kernel::font::vm {

// Execution limits.
constexpr uint32_t MaxStack = 4096;
constexpr uint32_t MaxNesting = 64;
constexpr uint32_t MaxWork = 1024 * 1024;
constexpr uint32_t MaxStorage = 65535;
constexpr uint32_t MaxCvt = undos::font::MaxProgramBytes / 2;
constexpr uint32_t MaxFunctions = 256;
constexpr uint32_t MaxInstructionDefs = 256;
constexpr uint32_t MaxCalls = 32;
constexpr uint32_t MaxZonePoints = 65535;
constexpr uint32_t InterpreterVersion = 35;

// F26Dot6 coordinates: 64 units per pixel.
constexpr int32_t PixelBits = 6;
constexpr int32_t Pixel = 1 << PixelBits;
constexpr int32_t HalfPixel = Pixel / 2;
constexpr int32_t QuarterPixel = Pixel / 4;

// F2Dot14 vector components: 16384 is 1.0.
constexpr int32_t UnitBits = 14;
constexpr int32_t UnitVector = 1 << UnitBits;
constexpr int32_t UnitRounding = UnitVector / 2;
constexpr int32_t Diagonal45Period = 0x2D41; // sqrt(2) / 2 in F2Dot14.
// Below this F2Dot14 product of freedom and projection, a move is treated as
// parallel instead of being amplified (FreeType uses the same bound).
constexpr int64_t MinFreedomProjection = 0x400;

constexpr uint32_t DefaultLoop = 1;

// Zone numbers selected by SZP0, SZP1, SZP2 and SZPS.
constexpr uint32_t TwilightZone = 0;
constexpr uint32_t GlyphZone = 1;
constexpr uint32_t ZonePointerCount = 3;
constexpr uint32_t ReferencePointCount = 3;

// Index into Geometry::zone for the pointer each opcode family reads.
constexpr uint32_t Zp0 = 0;
constexpr uint32_t Zp1 = 1;
constexpr uint32_t Zp2 = 2;
// Index into Geometry::reference.
constexpr uint32_t Rp0 = 0;
constexpr uint32_t Rp1 = 1;
constexpr uint32_t Rp2 = 2;

// Bits of Zone::touched.
constexpr uint8_t TouchedX = 1;
constexpr uint8_t TouchedY = 2;
constexpr uint8_t TouchedBoth = TouchedX | TouchedY;

// Bits of State::instructionControl, and INSTCTRL selectors.
constexpr uint32_t InhibitGlyphHinting = 1;
constexpr uint32_t UseDefaultParameters = 2;
constexpr int32_t InstructionControlSelectorMin = 1;
constexpr int32_t InstructionControlSelectorMax = 3;
constexpr int32_t InstructionControlNativeClearType = 3;

// GETINFO selector bit that requests the interpreter version.
constexpr uint32_t GetInfoVersionBit = 1;

// Parameter defaults and limits.
constexpr int32_t DefaultCvtCutIn = 68; // 17/16 pixel.
constexpr int32_t DefaultMinimumDistance = Pixel;
constexpr uint32_t DefaultDeltaBase = 9;
constexpr uint32_t DefaultDeltaShift = 3;
constexpr uint32_t MaxDeltaShiftSetting = 31;
constexpr uint32_t MaxDeltaShift = 6;
constexpr uint32_t MaxScanType = 7;
constexpr uint32_t Word16Mask = 0xffff;

// DELTA opcode argument layout: high nibble is the ppem offset, low nibble the step.
constexpr uint32_t DeltaPpemMask = 0xf0;
constexpr uint32_t DeltaPpemShift = 4;
constexpr uint32_t DeltaStepMask = 0x0f;
constexpr int32_t DeltaStepBias = 8;
constexpr uint32_t DeltaPpemRange = 16; // Each DELTA variant covers 16 sizes.

// MPS reports points per ppem at a fixed 96 DPI.
constexpr uint32_t PointSizePerPpem = 48;

// An NPUSHB/NPUSHW instruction is the opcode plus a one-byte count.
constexpr uint32_t PushHeaderSize = 2;
constexpr uint32_t ByteSize = 1;
constexpr uint32_t WordSize = 2;
constexpr uint32_t VectorComponents = 2;

// Stack word decoding.
constexpr int32_t WordSignBit = 0x8000;
constexpr int32_t WordRange = 0x10000;
constexpr int32_t ByteRange = 256;

// SROUND and S45ROUND selector layout.
constexpr uint32_t SuperGridPeriod = UnitVector;
constexpr int32_t SuperPeriodMask = 0xc0;
constexpr int32_t SuperPeriodHalf = 0x00;
constexpr int32_t SuperPeriodDouble = 0x80;
constexpr int32_t SuperPhaseMask = 0x30;
constexpr int32_t SuperPhaseQuarter = 0x10;
constexpr int32_t SuperPhaseHalf = 0x20;
constexpr int32_t SuperPhaseThreeQuarters = 0x30;
constexpr int32_t SuperThresholdMask = 0x0f;
constexpr int32_t SuperThresholdBias = 4;
constexpr int32_t SuperThresholdDivisor = 8;
constexpr int32_t SuperFixedShift = 8; // F2Dot14 settings to F26Dot6.

// ISECT treats lines as grazing below this ratio of dot product to discriminant.
constexpr int64_t IntersectGrazingRatio = 19;

// Integer square root and normalization precision.
constexpr uint64_t IsqrtStartBit = static_cast<uint64_t>(1) << 62;
constexpr int32_t NormalizeLimitBits = 15;
constexpr int64_t NormalizeLimit = static_cast<int64_t>(1) << NormalizeLimitBits;
// The length keeps this many fractional bits so quotients round, not truncate.
constexpr int32_t NormalizeFractionBits = 16;
constexpr int32_t NormalizeSquareShift = 2 * NormalizeFractionBits;
constexpr int64_t NormalizeFractionScale = static_cast<int64_t>(1) << NormalizeFractionBits;

// ISECT and ALIGNPTS operand counts, and the dot-product scale of ISECT.
constexpr uint32_t IntersectOperands = 5;
constexpr int64_t IntersectScale = Pixel;
constexpr int64_t IntersectAverageCount = 4;
} // namespace kernel::font::vm
