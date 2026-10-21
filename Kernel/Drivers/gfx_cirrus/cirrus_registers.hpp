#pragma once

#include <Kernel.hpp>

// Cirrus Logic CL-GD54xx registers, as exposed by QEMU's isa-cirrus-vga.
// Everything here is reached through the standard VGA port window
// (0x3C0-0x3DF); the ISA variant has no linear framebuffer, so VRAM is only
// visible through the 64 KiB window at 0xA0000. Kept private to gfx_cirrus.
namespace cirrus_regs {

// region I/O ports
constexpr uint16_t kMiscOutputWrite = 0x3C2;

constexpr uint16_t kSequencerIndex = 0x3C4;
constexpr uint16_t kSequencerData = 0x3C5;

constexpr uint16_t kDacMask = 0x3C6;// also the "hidden DAC" register
constexpr uint16_t kDacWriteIndex = 0x3C8;
constexpr uint16_t kDacData = 0x3C9;

constexpr uint16_t kGraphicsControllerIndex = 0x3CE;
constexpr uint16_t kGraphicsControllerData = 0x3CF;

constexpr uint16_t kCrtcIndex = 0x3D4;// color CRTC (mono would be 0x3B4)
constexpr uint16_t kCrtcData = 0x3D5;

constexpr uint16_t kAttributeControllerIndexData = 0x3C0;
constexpr uint16_t kInputStatus1 = 0x3DA;
// endregion

// region Sequencer extension registers
constexpr uint8_t kSrUnlockExtensions = 0x06;// write 0x12 to unlock, reads back 0x12
constexpr uint8_t kSrUnlockMagic = 0x12;
constexpr uint8_t kSr01ScreenOff = 0x20;          // SR01 bit 5: blank the display
constexpr uint8_t kSrExtendedSequencerMode = 0x07;// SVGA enable + colour depth
constexpr uint8_t kSrVclk0Numerator = 0x0E;
constexpr uint8_t kSrVclk0Denominator = 0x1E;
// endregion

// region CRTC extension registers
constexpr uint8_t kCrOverflow = 0x07;
constexpr uint8_t kCrStartAddressHigh = 0x0C;
constexpr uint8_t kCrStartAddressLow = 0x0D;
constexpr uint8_t kCrVerticalDisplayEnd = 0x12;
constexpr uint8_t kCrOffset = 0x13;         // pitch / 8
constexpr uint8_t kCrExtendedDisplay = 0x1B;// pitch + start address overflow bits
constexpr uint8_t kCrChipId = 0x27;
// endregion

// region Graphics controller extension registers
constexpr uint8_t kGrBackgroundColourByte0 = 0x00;
constexpr uint8_t kGrForegroundColourByte0 = 0x01;
constexpr uint8_t kGrOffset0 = 0x09;     // bank 0 offset, in 16 KiB (or 4 KiB) units
constexpr uint8_t kGrOffset1 = 0x0A;     // bank 1 offset
constexpr uint8_t kGrGraphicsMode = 0x0B;// dual-bank / granularity control
constexpr uint8_t kGrBackgroundColourByte1 = 0x10;
constexpr uint8_t kGrForegroundColourByte1 = 0x11;
constexpr uint8_t kGrBackgroundColourByte2 = 0x12;
constexpr uint8_t kGrForegroundColourByte2 = 0x13;
constexpr uint8_t kGrBackgroundColourByte3 = 0x14;
constexpr uint8_t kGrForegroundColourByte3 = 0x15;

// Single 64 KiB bank with 16 KiB granularity: bit0 (dual bank) clear,
// bit5 (16 KiB granularity) set.
constexpr uint8_t kGraphicsModeDualBank = 0x01;
constexpr uint8_t kGraphicsModeGranularity16k = 0x20;
constexpr uint32_t kBankGranularity = 16384;
constexpr uint32_t kWindowSize = 0x10000;
// endregion

// region BLT engine registers
constexpr uint8_t kGrBltWidthLow = 0x20;
constexpr uint8_t kGrBltWidthHigh = 0x21;
constexpr uint8_t kGrBltHeightLow = 0x22;
constexpr uint8_t kGrBltHeightHigh = 0x23;
constexpr uint8_t kGrBltDstPitchLow = 0x24;
constexpr uint8_t kGrBltDstPitchHigh = 0x25;
constexpr uint8_t kGrBltSrcPitchLow = 0x26;
constexpr uint8_t kGrBltSrcPitchHigh = 0x27;
constexpr uint8_t kGrBltDstAddrLow = 0x28;
constexpr uint8_t kGrBltDstAddrMid = 0x29;
constexpr uint8_t kGrBltDstAddrHigh = 0x2A;
constexpr uint8_t kGrBltSrcAddrLow = 0x2C;
constexpr uint8_t kGrBltSrcAddrMid = 0x2D;
constexpr uint8_t kGrBltSrcAddrHigh = 0x2E;
constexpr uint8_t kGrBltMode = 0x30;
constexpr uint8_t kGrBltStatus = 0x31;
constexpr uint8_t kGrBltRop = 0x32;
constexpr uint8_t kGrBltModeExtension = 0x33;

// GR30 (mode)
constexpr uint8_t kBltModeBackwards = 0x01;
constexpr uint8_t kBltModeMemSysDst = 0x02;
constexpr uint8_t kBltModeMemSysSrc = 0x04;
constexpr uint8_t kBltModeTransparentComp = 0x08;
constexpr uint8_t kBltModePatternCopy = 0x40;
constexpr uint8_t kBltModeColourExpand = 0x80;
constexpr uint8_t kBltModePixelWidth8 = 0x00;
constexpr uint8_t kBltModePixelWidth16 = 0x10;
constexpr uint8_t kBltModePixelWidth24 = 0x20;
constexpr uint8_t kBltModePixelWidth32 = 0x30;

// GR31 (status / start)
constexpr uint8_t kBltStatusStart = 0x02;
constexpr uint8_t kBltStatusBusy = 0x01;
constexpr uint8_t kBltStatusReset = 0x04;

// GR32 (raster operation)
constexpr uint8_t kBltRopSrcCopy = 0x0D;

// A system-memory-source transfer always consumes rows padded up to a
// multiple of four bytes, whatever the rectangle's real width is.
constexpr uint32_t kMemSysSrcRowAlignment = 4;

// GR33 (mode extension)
constexpr uint8_t kBltModeExtSolidFill = 0x04;
// endregion

// region Colour depth encodings
// SR07: bit0 enables SVGA (packed-pixel) mode, bits 3:1 select the depth.
constexpr uint8_t kSr07Svga = 0x01;
constexpr uint8_t kSr07Bpp8 = 0x00;
constexpr uint8_t kSr07Bpp16 = 0x06;
constexpr uint8_t kSr07Bpp24 = 0x04;
constexpr uint8_t kSr07Bpp32 = 0x08;

// Hidden DAC value: 0x00 for 8bpp/24bpp/32bpp, 0xC0 for RGB555 and 0xC1 for
// RGB565. Written after four reads of 0x3C6 (see Cirrus_WriteHiddenDac).
constexpr uint8_t kHiddenDacNone = 0x00;
constexpr uint8_t kHiddenDacRgb555 = 0xC0;
constexpr uint8_t kHiddenDacRgb565 = 0xC1;

struct CirrusDepth {
  uint32_t bpp;
  uint8_t sr07;
  uint8_t hiddenDac;
  uint8_t bltPixelWidth;
  kernel::PixelFormat pixelFormat;
};

// Ordered deepest first, so a memory-budget step-down walks it forwards.
constexpr CirrusDepth kDepths[] = {
    {.bpp = 32, .sr07 = static_cast<uint8_t>(kSr07Svga | kSr07Bpp32), .hiddenDac = kHiddenDacNone, .bltPixelWidth = kBltModePixelWidth32, .pixelFormat = kernel::PixelFormat::ARGB32},
    {.bpp = 24, .sr07 = static_cast<uint8_t>(kSr07Svga | kSr07Bpp24), .hiddenDac = kHiddenDacNone, .bltPixelWidth = kBltModePixelWidth24, .pixelFormat = kernel::PixelFormat::RGB24},
    {.bpp = 16, .sr07 = static_cast<uint8_t>(kSr07Svga | kSr07Bpp16), .hiddenDac = kHiddenDacRgb565, .bltPixelWidth = kBltModePixelWidth16, .pixelFormat = kernel::PixelFormat::RGB16},
    {.bpp = 15, .sr07 = static_cast<uint8_t>(kSr07Svga | kSr07Bpp16), .hiddenDac = kHiddenDacRgb555, .bltPixelWidth = kBltModePixelWidth16, .pixelFormat = kernel::PixelFormat::RGB15},
    {.bpp = 8, .sr07 = static_cast<uint8_t>(kSr07Svga | kSr07Bpp8), .hiddenDac = kHiddenDacNone, .bltPixelWidth = kBltModePixelWidth8, .pixelFormat = kernel::PixelFormat::INDEX8},
};

// endregion

// region Mode timings
struct CirrusModeTiming {
  uint32_t width;
  uint32_t height;
  uint32_t refreshRate;
  uint8_t crtc[25];    // CR00..CR18
  uint8_t crtcExtended;// CR1B, without the pitch overflow bit
  uint8_t vclkNumerator;
  uint8_t vclkDenominator;
};

// Standard VGA/SVGA CRTC dumps at 60 Hz. QEMU derives the visible geometry
// from CR01 and CR12 (plus the CR07 overflow bits) and ignores the rest of
// the timing, but the full table is programmed so real hardware syncs.
// CR13 (pitch) is overwritten per mode from the computed pitch.
constexpr CirrusModeTiming kModeTimings[] = {
    {.width = 640, .height = 480, .refreshRate = 60, .crtc = {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x8C, 0xDF, 0x50, 0x40, 0xE7, 0x04, 0xC3, 0xFF}, .crtcExtended = 0x22, .vclkNumerator = 0x4A, .vclkDenominator = 0x2B},
    {.width = 800, .height = 600, .refreshRate = 60, .crtc = {0x7F, 0x63, 0x64, 0x82, 0x6B, 0x1B, 0x72, 0xF0, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x59, 0x8D, 0x57, 0x64, 0x40, 0x58, 0x72, 0xE3, 0xFF}, .crtcExtended = 0x22, .vclkNumerator = 0x5B, .vclkDenominator = 0x2F},
    {.width = 1024, .height = 768, .refreshRate = 60, .crtc = {0xA3, 0x7F, 0x80, 0x86, 0x85, 0x96, 0x24, 0xFD, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x8A, 0xFF, 0x80, 0x40, 0x02, 0x22, 0xE3, 0xFF}, .crtcExtended = 0x22, .vclkNumerator = 0x42, .vclkDenominator = 0x1F},
};

// CR1B bit 4 carries bit 8 of the CR13 pitch value.
constexpr uint8_t kCrExtendedOffsetBit8 = 0x10;

// The fallback every failure path ends at: the smallest supported mode.
constexpr uint32_t kFallbackWidth = 640;
constexpr uint32_t kFallbackHeight = 480;
constexpr uint32_t kFallbackBpp = 8;
constexpr uint32_t kFallbackRefreshRate = 60;
// endregion

}// namespace cirrus_regs
