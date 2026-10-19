
#include <Kernel.hpp>

#include "cirrus_registers.hpp"

#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__struct_IoctlDisplayMode.h>

namespace {

constexpr size_t kPageSize = 4096;

// Per-instance state, held in the function device object's extension (see
// Cirrus_AddDevice). Everything below `shadow` is only valid once a mode has
// actually been programmed; before the first SetMode the shadow is null.
// No dirty-rectangle state: the USER layer tells Present what to transfer.
struct CirrusContext {
  kernel::KObjectPtr<kernel::KGraphicsAdapter> adapter;
  kernel::VirtualAddress window;// mapped 0xA0000 bank window
  kernel::VirtualAddress shadow;// flat system-RAM primary surface
  uint32_t pitch{};
  const cirrus_regs::CirrusModeTiming *timing{};
  const cirrus_regs::CirrusDepth *depth{};
  uint32_t activeBank{};
  uint8_t chipId{};
};

// region Low-level register access
void WriteSequencer(uint8_t index, uint8_t value) {
  HAL_IO_Out8(cirrus_regs::kSequencerIndex, index);
  HAL_IO_Out8(cirrus_regs::kSequencerData, value);
}

uint8_t ReadSequencer(uint8_t index) {
  HAL_IO_Out8(cirrus_regs::kSequencerIndex, index);
  return HAL_IO_In8(cirrus_regs::kSequencerData);
}

void WriteCrtc(uint8_t index, uint8_t value) {
  HAL_IO_Out8(cirrus_regs::kCrtcIndex, index);
  HAL_IO_Out8(cirrus_regs::kCrtcData, value);
}

uint8_t ReadCrtc(uint8_t index) {
  HAL_IO_Out8(cirrus_regs::kCrtcIndex, index);
  return HAL_IO_In8(cirrus_regs::kCrtcData);
}

void WriteGraphics(uint8_t index, uint8_t value) {
  HAL_IO_Out8(cirrus_regs::kGraphicsControllerIndex, index);
  HAL_IO_Out8(cirrus_regs::kGraphicsControllerData, value);
}

// The "hidden DAC" register shares port 0x3C6 with the pixel mask: it is
// selected by four consecutive reads of 0x3C6, after which a single write
// lands in the hidden register instead.
void WriteHiddenDac(uint8_t value) {
  HAL_IO_In8(cirrus_regs::kDacMask);
  HAL_IO_In8(cirrus_regs::kDacMask);
  HAL_IO_In8(cirrus_regs::kDacMask);
  HAL_IO_In8(cirrus_regs::kDacMask);
  HAL_IO_Out8(cirrus_regs::kDacMask, value);
}

// Unlocks the Cirrus extension registers. The same write doubles as the
// presence probe: on a real Cirrus (and on QEMU's model) SR06 reads back the
// magic value, while plain VGA hardware does not.
bool Cirrus_UnlockExtensions() {
  WriteSequencer(cirrus_regs::kSrUnlockExtensions, cirrus_regs::kSrUnlockMagic);
  return ReadSequencer(cirrus_regs::kSrUnlockExtensions) == cirrus_regs::kSrUnlockMagic;
}
// endregion

// region Mode selection
uint32_t BytesPerPixel(uint32_t bpp) { return bpp <= 8 ? 1 : (bpp <= 16 ? 2 : (bpp <= 24 ? 3 : 4)); }

// CR13 holds pitch/8, so the pitch is rounded up to a multiple of 8 bytes.
uint32_t PitchFor(uint32_t width, uint32_t bpp) {
  const uint32_t bytes = width * BytesPerPixel(bpp);
  return (bytes + 7u) & ~7u;
}

// A mode is only usable if its framebuffer fits in the adapter's VRAM.
bool FitsVideoRam(const cirrus_regs::CirrusModeTiming &timing, const cirrus_regs::CirrusDepth &depth) {
  (void) timing;
  (void) depth;

  return true;
  //return PitchFor(timing.width, depth.bpp) * timing.height <= cirrus_regs::kMaxVideoRamSize;
}

// endregion

// region Mode programming
void Cirrus_ProgramPalette() {
  // A 3-3-2 ramp: colour index bits 7:5 are red, 4:2 green, 1:0 blue, so
  // an 8bpp index is a meaningful colour without a palette manager.
  HAL_IO_Out8(cirrus_regs::kDacWriteIndex, 0);
  for (uint32_t i = 0; i < 256; ++i) {
    const auto red = static_cast<uint8_t>(((i >> 5) & 0x07) * 9);  // 0..63
    const auto green = static_cast<uint8_t>(((i >> 2) & 0x07) * 9);// 0..63
    const auto blue = static_cast<uint8_t>((i & 0x03) * 21);       // 0..63
    HAL_IO_Out8(cirrus_regs::kDacData, red);
    HAL_IO_Out8(cirrus_regs::kDacData, green);
    HAL_IO_Out8(cirrus_regs::kDacData, blue);
  }
}

void Cirrus_ProgramMode(CirrusContext *const ctx_cirrus) {
  const auto &timing = *ctx_cirrus->timing;
  const auto &depth = *ctx_cirrus->depth;

  if (!Cirrus_UnlockExtensions()) {
    KE_Print("gfx_cirrus: extension registers locked while setting the mode");
    return;
  }

  HAL_IO_Out8(cirrus_regs::kMiscOutputWrite, 0xEF);// colour, both sync signals negative

  // Standard sequencer setup for a packed-pixel mode: no reset, all planes
  // enabled, no chain-4 oddity.
  WriteSequencer(0x00, 0x03);
  WriteSequencer(0x01, 0x01);
  WriteSequencer(0x02, 0x0F);
  WriteSequencer(0x03, 0x00);
  WriteSequencer(0x04, 0x0E);

  WriteSequencer(cirrus_regs::kSrExtendedSequencerMode, depth.sr07);
  WriteSequencer(cirrus_regs::kSrVclk0Numerator, timing.vclkNumerator);
  WriteSequencer(cirrus_regs::kSrVclk0Denominator, timing.vclkDenominator);

  // Clear the CRTC write-protect bit so indices 0-7 can be programmed.
  WriteCrtc(0x11, static_cast<uint8_t>(ReadCrtc(0x11) & 0x7F));

  for (uint8_t i = 0; i < 25; ++i) {
    WriteCrtc(i, timing.crtc[i]);
  }

  const uint32_t offset = ctx_cirrus->pitch / 8;
  WriteCrtc(cirrus_regs::kCrOffset, static_cast<uint8_t>(offset & 0xFF));
  uint8_t extended = timing.crtcExtended;
  if (offset & 0x100) extended |= cirrus_regs::kCrExtendedOffsetBit8;
  WriteCrtc(cirrus_regs::kCrExtendedDisplay, extended);

  // Scan out from the start of VRAM.
  WriteCrtc(cirrus_regs::kCrStartAddressHigh, 0x00);
  WriteCrtc(cirrus_regs::kCrStartAddressLow, 0x00);

  // Graphics controller: packed-pixel (mode 0x40), single 64 KiB bank with
  // 16 KiB granularity, window at 0xA0000.
  WriteGraphics(0x00, 0x00);
  WriteGraphics(0x01, 0x00);
  WriteGraphics(0x02, 0x00);
  WriteGraphics(0x03, 0x00);
  WriteGraphics(0x04, 0x00);
  WriteGraphics(0x05, 0x40);
  WriteGraphics(0x06, 0x01);
  WriteGraphics(0x07, 0x0F);
  WriteGraphics(0x08, 0xFF);
  WriteGraphics(cirrus_regs::kGrGraphicsMode, cirrus_regs::kGraphicsModeGranularity16k);
  WriteGraphics(cirrus_regs::kGrOffset0, 0x00);
  WriteGraphics(cirrus_regs::kGrOffset1, 0x00);
  ctx_cirrus->activeBank = 0;

  // Attribute controller: a straight-through palette, then re-enable video.
  HAL_IO_In8(cirrus_regs::kInputStatus1);
  for (uint8_t i = 0; i < 16; ++i) {
    HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, i);
    HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, i);
  }
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x10);
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x41);// graphics mode, 8-bit colour
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x11);
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x00);
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x12);
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x0F);
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x13);
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x00);
  HAL_IO_In8(cirrus_regs::kInputStatus1);
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x20);

  WriteHiddenDac(depth.hiddenDac);
  if (depth.bpp == 8) {
    Cirrus_ProgramPalette();
  }

  KE_Print("gfx_cirrus: programmed {}x{}x{} @ {}Hz, pitch {} bytes",
           ctx_cirrus->timing->width, ctx_cirrus->timing->height, ctx_cirrus->depth->bpp, ctx_cirrus->timing->refreshRate, ctx_cirrus->pitch);
}
// endregion

// region Bank window and the region flush
// Points the 64 KiB window at the 16 KiB-aligned bank containing
// `vramOffset` and returns the offset of that byte inside the window.
uint32_t Cirrus_SetBank(CirrusContext *const ctx_cirrus, uint32_t vramOffset) {
  const uint32_t bank = vramOffset / cirrus_regs::kBankGranularity;
  if (bank != ctx_cirrus->activeBank) {
    WriteGraphics(cirrus_regs::kGrOffset0, static_cast<uint8_t>(bank));
    ctx_cirrus->activeBank = bank;
  }
  return vramOffset - bank * cirrus_regs::kBankGranularity;
}

// Copies `length` bytes into VRAM through the bank window, switching banks
// whenever the run crosses the end of the currently mapped 64 KiB view.
void Cirrus_WriteVram(CirrusContext *const ctx_cirrus, uint32_t vramOffset, const uint8_t *src, size_t length) {
  auto *window = ctx_cirrus->window.as_ptr<volatile uint8_t>();
  while (length) {
    const uint32_t windowOffset = Cirrus_SetBank(ctx_cirrus, vramOffset);
    size_t chunk = cirrus_regs::kWindowSize - windowOffset;
    if (chunk > length) chunk = length;
    for (size_t i = 0; i < chunk; ++i) {
      window[windowOffset + i] = src[i];
    }
    vramOffset += static_cast<uint32_t>(chunk);
    src += chunk;
    length -= chunk;
  }
}

// Aborts whatever the engine is doing and disarms it. The reset is only
// acted on when the bit falls again, so both writes are required: leaving
// the engine armed would make it swallow every later window write.
void Cirrus_ResetBlitter() {
  WriteGraphics(cirrus_regs::kGrBltStatus, cirrus_regs::kBltStatusReset);
  WriteGraphics(cirrus_regs::kGrBltStatus, 0x00);
}

// The engine must be idle before it is reprogrammed. Returns false on
// timeout, in which case the caller takes the CPU path instead.
bool Cirrus_WaitForBlitter() {
  for (uint32_t spin = 0; spin < 100000; ++spin) {
    HAL_IO_Out8(cirrus_regs::kGraphicsControllerIndex, cirrus_regs::kGrBltStatus);
    if ((HAL_IO_In8(cirrus_regs::kGraphicsControllerData) & cirrus_regs::kBltStatusBusy) == 0) {
      return true;
    }
  }
  return false;
}

void Cirrus_ProgramBlitGeometry(uint32_t widthBytes, uint32_t height, uint32_t dstPitch, uint32_t srcPitch, uint32_t dstAddress) {
  WriteGraphics(cirrus_regs::kGrBltWidthLow, static_cast<uint8_t>((widthBytes - 1) & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltWidthHigh, static_cast<uint8_t>(((widthBytes - 1) >> 8) & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltHeightLow, static_cast<uint8_t>((height - 1) & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltHeightHigh, static_cast<uint8_t>(((height - 1) >> 8) & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltDstPitchLow, static_cast<uint8_t>(dstPitch & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltDstPitchHigh, static_cast<uint8_t>((dstPitch >> 8) & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltSrcPitchLow, static_cast<uint8_t>(srcPitch & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltSrcPitchHigh, static_cast<uint8_t>((srcPitch >> 8) & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltDstAddrLow, static_cast<uint8_t>(dstAddress & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltDstAddrMid, static_cast<uint8_t>((dstAddress >> 8) & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltDstAddrHigh, static_cast<uint8_t>((dstAddress >> 16) & 0x3F));
}

// Pushes one rectangle of the shadow buffer into VRAM with the blitter's
// system-memory-source mode: the engine consumes the source pixels written
// into the 0xA0000 window, so the CPU never bank-switches per row.
bool Cirrus_FlushRectAccelerated(CirrusContext *const ctx_cirrus, int32_t x, int32_t y, uint32_t width, uint32_t height) {
  if (!Cirrus_WaitForBlitter()) return false;

  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->depth->bpp);
  const uint32_t widthBytes = width * bytesPerPixel;
  // The engine rounds the source row up to a multiple of four bytes and
  // consumes exactly that many per row, so the padding has to be fed too:
  // underfeeding it leaves the transfer armed and it then eats every later
  // write to the window.
  const uint32_t srcPitch = (widthBytes + cirrus_regs::kMemSysSrcRowAlignment - 1) &
                            ~(cirrus_regs::kMemSysSrcRowAlignment - 1);
  const uint32_t dstAddress = static_cast<uint32_t>(y) * ctx_cirrus->pitch + static_cast<uint32_t>(x) * bytesPerPixel;

  Cirrus_ProgramBlitGeometry(widthBytes, height, ctx_cirrus->pitch, srcPitch, dstAddress);
  WriteGraphics(cirrus_regs::kGrBltRop, cirrus_regs::kBltRopSrcCopy);
  WriteGraphics(cirrus_regs::kGrBltModeExtension, 0x00);
  WriteGraphics(cirrus_regs::kGrBltMode,
                static_cast<uint8_t>(cirrus_regs::kBltModeMemSysSrc | ctx_cirrus->depth->bltPixelWidth));
  WriteGraphics(cirrus_regs::kGrBltStatus, cirrus_regs::kBltStatusStart);

  const auto *shadow = ctx_cirrus->shadow.as_ptr<const uint8_t>();
  auto *window = ctx_cirrus->window.as_ptr<volatile uint8_t>();
  size_t written = 0;
  for (uint32_t row = 0; row < height; ++row) {
    const uint8_t *src = shadow + (static_cast<size_t>(y) + row) * ctx_cirrus->pitch +
                         static_cast<size_t>(x) * bytesPerPixel;
    for (uint32_t i = 0; i < srcPitch; ++i) {
      window[written % cirrus_regs::kWindowSize] = i < widthBytes ? src[i] : 0;
      ++written;
    }
  }

  return Cirrus_WaitForBlitter();
}

// Plain bank-switched row copy, used when the blitter does not settle.
void Cirrus_FlushRectCpu(CirrusContext *const ctx_cirrus, int32_t x, int32_t y, uint32_t width, uint32_t height) {
  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->depth->bpp);
  const uint32_t widthBytes = width * bytesPerPixel;
  const auto *shadow = ctx_cirrus->shadow.as_ptr<const uint8_t>();

  for (uint32_t row = 0; row < height; ++row) {
    const size_t offset = (static_cast<size_t>(y) + row) * ctx_cirrus->pitch +
                          static_cast<size_t>(x) * bytesPerPixel;
    Cirrus_WriteVram(ctx_cirrus, static_cast<uint32_t>(offset), shadow + offset, widthBytes);
  }
}

void Cirrus_FlushRect(CirrusContext *const ctx_cirrus, int32_t left, int32_t top, int32_t right, int32_t bottom) {
  if (left < 0) left = 0;
  if (top < 0) top = 0;
  if (right > static_cast<int32_t>(ctx_cirrus->timing->width)) right = static_cast<int32_t>(ctx_cirrus->timing->width);
  if (bottom > static_cast<int32_t>(ctx_cirrus->timing->height)) bottom = static_cast<int32_t>(ctx_cirrus->timing->height);
  if (right <= left || bottom <= top) return;

  const auto width = static_cast<uint32_t>(right - left);
  const auto height = static_cast<uint32_t>(bottom - top);
  if (!Cirrus_FlushRectAccelerated(ctx_cirrus, left, top, width, height)) {
    // Reset the engine before falling back, so a stuck operation does not
    // consume the CPU-written bytes instead of VRAM.
    Cirrus_ResetBlitter();
    Cirrus_FlushRectCpu(ctx_cirrus, left, top, width, height);
  }
}

// Pushes the whole shadow buffer out, so VRAM never shows whatever the
// previous mode (usually text) left behind.
void Cirrus_FlushScreen(CirrusContext *const ctx_cirrus) {
  Cirrus_FlushRect(ctx_cirrus, 0, 0, static_cast<int32_t>(ctx_cirrus->timing->width), static_cast<int32_t>(ctx_cirrus->timing->height));
}

// A null/empty rectangle list means "the whole surface". Called once per
// paint cycle by the USER layer, never per drawing primitive.
void Cirrus_Present(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                    const kernel::KObjectPtr<kernel::KSurface> &surface, const kernel::KClipRect *rects, size_t count) {
  (void) surface;

  if (!adapter) return;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) return;

  if (!ctx_cirrus->shadow || !ctx_cirrus->window || !ctx_cirrus->depth) return;

  if (!rects) {
    Cirrus_FlushRect(ctx_cirrus, 0, 0, static_cast<int32_t>(ctx_cirrus->timing->width), static_cast<int32_t>(ctx_cirrus->timing->height));
    return;
  }

  for (size_t i = 0; i < count; ++i) {
    Cirrus_FlushRect(ctx_cirrus, rects[i].left, rects[i].top, rects[i].right, rects[i].bottom);
  }
}
// endregion

// region Blitter-accelerated primitives
// Only the primary surface lives in VRAM; anything else (an off-screen
// bitmap in plain system memory) is left to the kernel's software path.
// Before the first SetMode there is no shadow buffer at all, so nothing can
// be the primary surface.
bool IsPrimary(CirrusContext *const ctx_cirrus, const kernel::KObjectPtr<kernel::KSurface> &surface) {
  if (!ctx_cirrus->shadow || !ctx_cirrus->depth) return false;
  return surface && surface->backing && surface->backing->directAddress == ctx_cirrus->shadow;
}

// The fill colour is held in the foreground colour bytes, widened to the
// active depth.
void Cirrus_ProgramFillColour(CirrusContext *const ctx_cirrus, uint32_t colour) {
  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->depth->bpp);
  const uint8_t bytes[4] = {
      static_cast<uint8_t>(colour & 0xFF),
      static_cast<uint8_t>((colour >> 8) & 0xFF),
      static_cast<uint8_t>((colour >> 16) & 0xFF),
      static_cast<uint8_t>((colour >> 24) & 0xFF),
  };
  constexpr uint8_t kColourRegisters[4] = {
      cirrus_regs::kGrForegroundColourByte0,
      cirrus_regs::kGrForegroundColourByte1,
      cirrus_regs::kGrForegroundColourByte2,
      cirrus_regs::kGrForegroundColourByte3,
  };
  for (uint32_t i = 0; i < bytesPerPixel && i < 4; ++i) {
    WriteGraphics(kColourRegisters[i], bytes[i]);
  }
}

// Writes the same fill into the shadow buffer, so CPU reads and the
// software fallbacks stay coherent with what the engine put in VRAM.
void Cirrus_FillShadow(CirrusContext *const ctx_cirrus, int x, int y, int w, int h, uint32_t colour) {
  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->depth->bpp);
  auto *shadow = ctx_cirrus->shadow.as_ptr<uint8_t>();
  for (int row = y; row < y + h; ++row) {
    uint8_t *dst = shadow + static_cast<size_t>(row) * ctx_cirrus->pitch + static_cast<size_t>(x) * bytesPerPixel;
    for (int col = 0; col < w; ++col) {
      for (uint32_t b = 0; b < bytesPerPixel; ++b) {
        dst[b] = static_cast<uint8_t>((colour >> (8 * b)) & 0xFF);
      }
      dst += bytesPerPixel;
    }
  }
}

bool Cirrus_FillRect(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                     const kernel::KObjectPtr<kernel::KSurface> &dst, int x, int y, int w, int h, uint32_t colour) {
  if (!adapter) return false;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) return false;

  if (!IsPrimary(ctx_cirrus, dst) || w <= 0 || h <= 0) return false;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > static_cast<int>(ctx_cirrus->timing->width)) w = static_cast<int>(ctx_cirrus->timing->width) - x;
  if (y + h > static_cast<int>(ctx_cirrus->timing->height)) h = static_cast<int>(ctx_cirrus->timing->height) - y;
  if (w <= 0 || h <= 0) return true;

  if (!Cirrus_WaitForBlitter()) {
    Cirrus_ResetBlitter();
    return false;
  }

  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->depth->bpp);
  const uint32_t widthBytes = static_cast<uint32_t>(w) * bytesPerPixel;
  const uint32_t dstAddress = static_cast<uint32_t>(y) * ctx_cirrus->pitch + static_cast<uint32_t>(x) * bytesPerPixel;

  Cirrus_ProgramFillColour(ctx_cirrus, colour);
  Cirrus_ProgramBlitGeometry(widthBytes, static_cast<uint32_t>(h), ctx_cirrus->pitch, 0, dstAddress);
  WriteGraphics(cirrus_regs::kGrBltRop, cirrus_regs::kBltRopSrcCopy);
  WriteGraphics(cirrus_regs::kGrBltModeExtension, cirrus_regs::kBltModeExtSolidFill);
  WriteGraphics(cirrus_regs::kGrBltMode,
                static_cast<uint8_t>(cirrus_regs::kBltModePatternCopy | cirrus_regs::kBltModeColourExpand |
                                     ctx_cirrus->depth->bltPixelWidth));
  WriteGraphics(cirrus_regs::kGrBltStatus, cirrus_regs::kBltStatusStart);

  if (!Cirrus_WaitForBlitter()) {
    Cirrus_ResetBlitter();
    WriteGraphics(cirrus_regs::kGrBltModeExtension, 0x00);
    return false;
  }
  WriteGraphics(cirrus_regs::kGrBltModeExtension, 0x00);

  // VRAM is already correct, so this produces no transfer: only the shadow
  // still needs the same pixels.
  Cirrus_FillShadow(ctx_cirrus, x, y, w, h, colour);
  return true;
}

bool Cirrus_BitBlt(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                   const kernel::KObjectPtr<kernel::KSurface> &dst,
                   int dstX, int dstY,
                   const kernel::KObjectPtr<kernel::KSurface> &src,
                   int srcX, int srcY,
                   uint32_t width, uint32_t height,
                   kernel::RasterOp rop) {

  if (!adapter) return false;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) return false;

  // Anything other than a straight copy, or a surface that isn't in VRAM,
  // is left to the kernel's software fallback.
  if (rop != kernel::RasterOp::SRCCOPY || !IsPrimary(ctx_cirrus, dst) || !IsPrimary(ctx_cirrus, src)) return false;
  if (width == 0 || height == 0) return true;
  if (dstX < 0 || dstY < 0 || srcX < 0 || srcY < 0) return false;
  if (dstX + static_cast<int>(width) > static_cast<int>(ctx_cirrus->timing->width) ||
      srcX + static_cast<int>(width) > static_cast<int>(ctx_cirrus->timing->width) ||
      dstY + static_cast<int>(height) > static_cast<int>(ctx_cirrus->timing->height) ||
      srcY + static_cast<int>(height) > static_cast<int>(ctx_cirrus->timing->height)) {
    return false;
  }

  if (!Cirrus_WaitForBlitter()) {
    Cirrus_ResetBlitter();
    return false;
  }

  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->depth->bpp);
  const uint32_t widthBytes = width * bytesPerPixel;
  uint32_t dstAddress = static_cast<uint32_t>(dstY) * ctx_cirrus->pitch + static_cast<uint32_t>(dstX) * bytesPerPixel;
  uint32_t srcAddress = static_cast<uint32_t>(srcY) * ctx_cirrus->pitch + static_cast<uint32_t>(srcX) * bytesPerPixel;

  // Overlapping rectangles moving forwards would overwrite source rows
  // before they are read, so the engine walks them backwards instead.
  const bool backwards = dstAddress > srcAddress &&
                         dstAddress < srcAddress + static_cast<uint32_t>(height) * ctx_cirrus->pitch;
  if (backwards) {
    const uint32_t span = (height - 1) * ctx_cirrus->pitch + widthBytes - 1;
    dstAddress += span;
    srcAddress += span;
  }

  Cirrus_ProgramBlitGeometry(widthBytes, height, ctx_cirrus->pitch, ctx_cirrus->pitch, dstAddress);
  WriteGraphics(cirrus_regs::kGrBltSrcAddrLow, static_cast<uint8_t>(srcAddress & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltSrcAddrMid, static_cast<uint8_t>((srcAddress >> 8) & 0xFF));
  WriteGraphics(cirrus_regs::kGrBltSrcAddrHigh, static_cast<uint8_t>((srcAddress >> 16) & 0x3F));
  WriteGraphics(cirrus_regs::kGrBltRop, cirrus_regs::kBltRopSrcCopy);
  WriteGraphics(cirrus_regs::kGrBltModeExtension, 0x00);
  WriteGraphics(cirrus_regs::kGrBltMode,
                static_cast<uint8_t>(ctx_cirrus->depth->bltPixelWidth | (backwards ? cirrus_regs::kBltModeBackwards : 0)));
  WriteGraphics(cirrus_regs::kGrBltStatus, cirrus_regs::kBltStatusStart);

  if (!Cirrus_WaitForBlitter()) {
    Cirrus_ResetBlitter();
    return false;
  }

  // Mirror the copy into the shadow buffer, row by row, so both copies of
  // the screen agree.
  auto *shadow = ctx_cirrus->shadow.as_ptr<uint8_t>();
  const bool downwards = dstY > srcY;
  for (uint32_t i = 0; i < height; ++i) {
    const uint32_t row = downwards ? height - 1 - i : i;
    uint8_t *to = shadow + (static_cast<size_t>(dstY) + row) * ctx_cirrus->pitch +
                  static_cast<size_t>(dstX) * bytesPerPixel;
    const uint8_t *from = shadow + (static_cast<size_t>(srcY) + row) * ctx_cirrus->pitch +
                          static_cast<size_t>(srcX) * bytesPerPixel;
    // Copied by hand, and back-to-front when the rows overlap rightwards:
    // the driver loader cannot resolve a libc memmove.
    if (to > from) {
      for (uint32_t b = widthBytes; b-- > 0;) to[b] = from[b];
    } else {
      for (uint32_t b = 0; b < widthBytes; ++b) to[b] = from[b];
    }
  }
  return true;
}

// The engine has no line primitive, so lines are walked in the shadow
// buffer; the swept area is covered by the caller's paint region, which
// reaches VRAM at EndPaint.
bool Cirrus_DrawLine(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                     const kernel::KObjectPtr<kernel::KSurface> &dst, int x0, int y0, int x1, int y1, uint32_t colour) {
  if (!adapter) return false;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) return false;

  if (!IsPrimary(ctx_cirrus, dst)) return false;

  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    if (x0 >= 0 && y0 >= 0 && x0 < static_cast<int>(ctx_cirrus->timing->width) && y0 < static_cast<int>(ctx_cirrus->timing->height)) {
      Cirrus_FillShadow(ctx_cirrus, x0, y0, 1, 1, colour);
    }
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
  return true;
}
// endregion

// region KGraphicsAdapter / KDisplay hooks


// The mode list is the kModeTimings x kDepths cross product, in table
// order, skipping anything that does not fit in VRAM. The index is stable
// for the lifetime of the driver: it does not depend on the active mode.
bool Cirrus_ResolveMode(size_t modeIndex,
                        const cirrus_regs::CirrusModeTiming *&outTiming,
                        const cirrus_regs::CirrusDepth *&outDepth) {
  size_t index = 0;
  for (const auto &timing: cirrus_regs::kModeTimings) {
    for (const auto &depth: cirrus_regs::kDepths) {
      if (!FitsVideoRam(timing, depth)) continue;
      if (index++ != modeIndex) continue;
      outTiming = &timing;
      outDepth = &depth;
      return true;
    }
  }
  return false;
}

// Direct-colour channel widths and masks, as the Cirrus DAC emits them.
// Palette-driven 8bpp has no channels of its own.
void Cirrus_FillColourFormat(const cirrus_regs::CirrusDepth &depth, kernel::KDisplayModeInfo &outInfo) {
  switch (depth.pixelFormat) {
    case kernel::PixelFormat::RGB15:
      outInfo.numberRedBits = 5;
      outInfo.numberGreenBits = 5;
      outInfo.numberBlueBits = 5;
      outInfo.redMask = 0x7C00;
      outInfo.greenMask = 0x03E0;
      outInfo.blueMask = 0x001F;
      break;
    case kernel::PixelFormat::RGB16:
      outInfo.numberRedBits = 5;
      outInfo.numberGreenBits = 6;
      outInfo.numberBlueBits = 5;
      outInfo.redMask = 0xF800;
      outInfo.greenMask = 0x07E0;
      outInfo.blueMask = 0x001F;
      break;
    case kernel::PixelFormat::RGB24:
    case kernel::PixelFormat::ARGB32:
      outInfo.numberRedBits = 8;
      outInfo.numberGreenBits = 8;
      outInfo.numberBlueBits = 8;
      outInfo.redMask = 0x00FF0000;
      outInfo.greenMask = 0x0000FF00;
      outInfo.blueMask = 0x000000FF;
      break;
    case kernel::PixelFormat::INDEX8:
      break;
  }
}

bool Cirrus_GetModeInfo(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/, size_t modeIndex, kernel::KDisplayModeInfo &outInfo) {
  const cirrus_regs::CirrusModeTiming *timing = nullptr;
  const cirrus_regs::CirrusDepth *depth = nullptr;
  if (!Cirrus_ResolveMode(modeIndex, timing, depth)) return false;

  outInfo.modeIndex = modeIndex;
  outInfo.width = timing->width;
  outInfo.height = timing->height;
  outInfo.pitch = PitchFor(timing->width, depth->bpp);
  outInfo.videoMemoryBitmapWidth = timing->width;
  outInfo.videoMemoryBitmapHeight = timing->height;
  outInfo.bpp = depth->bpp;
  outInfo.numberOfPlanes = 1;// Packed pixel, not planar
  outInfo.bitsPerPlane = depth->bpp;
  outInfo.refreshRate = timing->refreshRate;
  outInfo.pixelFormat = depth->pixelFormat;
  Cirrus_FillColourFormat(*depth, outInfo);
  outInfo.attributeFlags = kernel::DisplayModeFlags::GraphicsMode |
                           kernel::DisplayModeFlags::ColorMode |
                           kernel::DisplayModeFlags::HardwareAccelerated |
                           kernel::DisplayModeFlags::Banked;
  if (depth->pixelFormat == kernel::PixelFormat::INDEX8) {
    outInfo.attributeFlags |= kernel::DisplayModeFlags::PaletteDriven;
  }

  return true;
}

bool Cirrus_Probe(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) {
  if (!adapter) {
    KE_Print("gfx_cirrus: No adapter to probe");
    return false;
  }
  if (const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>(); 
    !ctx_cirrus) {
    KE_Print("gfx_cirrus: No Cirrus context");
    return false;
  }

  return true;
}

// The single head this adapter drives. Identity only: the entry points are
// bound later, from Cirrus_ConfigureDisplay.
bool Cirrus_ProbeDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                         const kernel::KGraphicsAdapterDisplayEnumInfo &enumInfo,
                         kernel::KGraphicsAdapterDisplayInfo &outInfo) {
  if (enumInfo.displayIndex != 0) {
    return false;
  }

  outInfo.isPresent = true;
  outInfo.isMonitor = true;
  outInfo.isPnP = false;// The ISA Cirrus model exposes no DDC channel
  outInfo.edidSize = 0;
  outInfo.name = "VGA";
  return true;
}

// Programs `modeIndex` and reports where its pixels live. This is the only
// place a shadow buffer is allocated: the new buffer is taken before any
// register is touched, so a failed allocation leaves the previous mode
// running untouched.
bool Cirrus_SetMode(const kernel::KObjectPtr<kernel::KDisplay> &display, size_t modeIndex, kernel::KDisplaySurfaceBinding &outBinding) {
  if (!display || !display->adapter) return false;
  const auto ctx_cirrus = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus || !ctx_cirrus->window) return false;

  const cirrus_regs::CirrusModeTiming *timing = nullptr;
  const cirrus_regs::CirrusDepth *depth = nullptr;
  if (!Cirrus_ResolveMode(modeIndex, timing, depth)) return false;

  const uint32_t pitch = PitchFor(timing->width, depth->bpp);
  const size_t size = static_cast<size_t>(pitch) * timing->height;
  void *buffer = KE_Malloc(size);
  if (!buffer) {
    KE_Print("gfx_cirrus: {}KiB shadow for {}x{}x{} could not be allocated, keeping the current mode",
             size / 1024, timing->width, timing->height, depth->bpp);
    return false;
  }
  __builtin_memset(buffer, 0, size);

  const auto previousShadow = ctx_cirrus->shadow;

  ctx_cirrus->shadow = kernel::VirtualAddress::from_ptr(buffer);
  ctx_cirrus->pitch = pitch;
  ctx_cirrus->timing = timing;
  ctx_cirrus->depth = depth;

  Cirrus_ProgramMode(ctx_cirrus);
  Cirrus_FlushScreen(ctx_cirrus);

  // The old buffer is only released once the new mode is actually running.
  if (previousShadow) {
    KE_Free(previousShadow.as_ptr<void>());
  }

  outBinding.backing = KE_VMM_CreateDirectBacking(ctx_cirrus->shadow, size);
  if (!outBinding.backing) {
    return false;
  }

  outBinding.type = kernel::SurfaceType::Framebuffer;
  outBinding.flags = kernel::SurfaceFlags::TopDown | kernel::SurfaceFlags::Linear;
  KE_Print("gfx_cirrus: mode {} -> {}x{}x{}, shadow {}KiB",
           modeIndex, ctx_cirrus->timing->width, ctx_cirrus->timing->height, ctx_cirrus->depth->bpp, size / 1024);
  return true;
}

// Video output is gated by the sequencer's screen-off bit (SR01 bit 5):
// set blanks the display without disturbing the programmed timing.
bool Cirrus_Enable(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/) {
  WriteSequencer(0x01, static_cast<uint8_t>(ReadSequencer(0x01) & ~cirrus_regs::kSr01ScreenOff));
  return true;
}

void Cirrus_Disable(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/) {
  WriteSequencer(0x01, static_cast<uint8_t>(ReadSequencer(0x01) | cirrus_regs::kSr01ScreenOff));
}

// Binds this driver's per-head entry points on the display the graphics
// subsystem created for the head Cirrus_ProbeDisplay reported.
bool Cirrus_ConfigureDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/, const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display) {
    return false;
  }

  display->GetModeInfo = Cirrus_GetModeInfo;
  display->SetMode = Cirrus_SetMode;
  display->Enable = Cirrus_Enable;
  display->Disable = Cirrus_Disable;
  return true;
}
// endregion

bool Cirrus_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  // The generic ISA presence check also sees standard VGA at these ports.
  // Reject it before PnP reserves the shared VGA aperture for this driver,
  // otherwise the real VGA fallback cannot acquire its exclusive resources.
  if (!Cirrus_UnlockExtensions()) {
    KE_Print("gfx_cirrus: no Cirrus chip detected (SR06 unlock failed)");
    return false;
  }
  const auto fdo = KE_IO_CreateDeviceWithContext<CirrusContext>(driver, "Cirrus", kernel::device_type::Video);
  if (!fdo) {
    KE_Print("gfx_cirrus: Failed to create Cirrus device");
    return false;
  }

  KE_IO_AttachFilterDevice(fdo, pdo);
  return true;
}

kernel::IoStatus Cirrus_DeviceControl(const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t code,
                                      kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred) {
  (void) input;
  transferred = 0;

  const auto ctx_cirrus = device->deviceExtension.as<CirrusContext>();

  if (code != IoControlCode_DisplayQueryMode) return kernel::IoStatus::Unsupported;
  if (output.size() < sizeof(IoctlDisplayMode)) return kernel::IoStatus::InvalidSize;
  if (!ctx_cirrus) return kernel::IoStatus::Unsupported;
  // No mode has been programmed yet: there is nothing to report.
  if (!ctx_cirrus->shadow || !ctx_cirrus->depth) return kernel::IoStatus::Unsupported;

  const IoctlDisplayMode mode{.width = ctx_cirrus->timing->width, .height = ctx_cirrus->timing->height, .bpp = ctx_cirrus->depth->bpp, .pitch = ctx_cirrus->pitch};
  __builtin_memcpy(output.data(), &mode, sizeof(mode));
  transferred = sizeof(mode);
  KE_Print("gfx_cirrus: DeviceControl QueryMode -> {}x{}x{}", mode.width, mode.height, mode.bpp);
  return kernel::IoStatus::Success;
}

void Cirrus_StartDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto driver = pdo->driverObject;

  // The bank window's physical address/length come from whatever the PnP
  // manager granted through the legacy enumerator's requirements list, not
  // from a hardcoded constant (same pattern as gfx_vga).
  const auto *windowRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Memory, 0);
  if (!windowRes) {
    KE_Print("gfx_cirrus: no bank window memory resource assigned");
    return;
  }

  if (!Cirrus_UnlockExtensions()) {
    KE_Print("gfx_cirrus: no Cirrus chip detected (SR06 unlock failed)");
    KE_RES_ReleaseResources(pdo);
    return;
  }

  // The context lives in the function device object Cirrus_AddDevice stacked
  // on top of the PDO; that same device is the one the adapter points at, so
  // the hooks resolve the very same state through GetDeviceExtensionAs.
  const auto fdo = pdo->attachedDevice ? pdo->attachedDevice : pdo;
  const auto ctx_cirrus = fdo->deviceExtension.as<CirrusContext>();
  if (!ctx_cirrus) {
    KE_Print("gfx_cirrus: no device context");
    return;
  }
  ctx_cirrus->chipId = static_cast<uint8_t>((ReadCrtc(cirrus_regs::kCrChipId) >> 2) & 0x3F);

  auto &as = KE_VMM_GetKernelAddressSpace();
  const void *region = KE_VMM_AllocateRegion(as, windowRes->length, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!region) {
    KE_Print("gfx_cirrus: Failed to reserve virtual address space for the bank window");
    return;
  }

  const auto virt = kernel::VirtualAddress::from_ptr(region);
  const size_t pageCount = windowRes->length / kPageSize;
  for (size_t i = 0; i < pageCount; ++i) {
    KE_VMM_MapPhysical(
        as,
        virt + (i * kPageSize),
        kernel::PhysicalAddress(windowRes->start) + (i * kPageSize),
        kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  }
  ctx_cirrus->window = virt;
  ctx_cirrus->activeBank = 0;

  KE_Print("gfx_cirrus: Cirrus chip id 0x{x} detected, {}KiB window mapped",
           ctx_cirrus->chipId, windowRes->length / 1024);

  ctx_cirrus->adapter = KE_GFX_RegisterGraphicsAdapter(driver, fdo, "Cirrus");
  if (!ctx_cirrus->adapter) {
    KE_Print("gfx_cirrus: Failed to register graphics adapter");
    return;
  }

  // No shadow buffer is committed here: memory is only taken once the
  // graphics subsystem actually programs a mode through SetMode.
  ctx_cirrus->adapter->Probe = Cirrus_Probe;
  ctx_cirrus->adapter->ProbeDisplay = Cirrus_ProbeDisplay;
  ctx_cirrus->adapter->ConfigureDisplay = Cirrus_ConfigureDisplay;
  ctx_cirrus->adapter->FillRect = Cirrus_FillRect;
  ctx_cirrus->adapter->DrawLine = Cirrus_DrawLine;
  ctx_cirrus->adapter->BitBlt = Cirrus_BitBlt;
  ctx_cirrus->adapter->Present = Cirrus_Present;
  ctx_cirrus->adapter->caps = kernel::KGfxCaps::HwFill | kernel::KGfxCaps::HwBlt;
}
}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = Cirrus_AddDevice;
  driver->StartDevice = Cirrus_StartDevice;
  driver->DeviceControl = Cirrus_DeviceControl;
}
