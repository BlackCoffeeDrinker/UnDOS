
#include <Kernel.hpp>

#include <kernel/gfx/video_present.hpp>

#include "cirrus_registers.hpp"
#include "cirrus_timing.hpp"

#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__struct_IoctlDisplayMode.h>

namespace {

constexpr size_t kPageSize = 4096;

// Per-instance state, held in the function device object's extension (see
// Cirrus_AddDevice). Everything below `shadow` is only valid once a mode has
// actually been programmed; before the first mode transaction the shadow is null.
// No dirty-rectangle state: the USER layer tells Present what to transfer.
struct CirrusContext {
  kernel::KObjectPtr<kernel::KGraphicsAdapter> adapter;
  kernel::VirtualAddress window;// mapped 0xA0000 bank window
  kernel::VirtualAddress shadow;// flat system-RAM primary surface
  uint32_t pitch{};
  const cirrus_regs::CirrusModeTiming *timing{};
  const cirrus_regs::CirrusDepth *depth{};
  cirrus_regs::CirrusModeTiming activeTiming{};
  cirrus_regs::CirrusDepth activeDepth{};
  uint32_t activeBank{};
  uint8_t chipId{};
  cirrus_timing::Chip chip{cirrus_timing::Chip::Unknown};
  uint32_t vramBytes{};
  bool modeKnown{false};
  bool enabled{false};
  size_t modeIndex{};
};

struct CirrusReservationData {
  kernel::VirtualAddress candidateShadow{};
  kernel::VirtualAddress previousShadow{};
  uint32_t previousPitch{};
  cirrus_regs::CirrusModeTiming candidateTiming{};
  cirrus_regs::CirrusDepth candidateDepth{};
  cirrus_regs::CirrusModeTiming previousTiming{};
  cirrus_regs::CirrusDepth previousDepth{};
  bool previousTimingValid{};
  bool previousDepthValid{};
  size_t previousModeIndex{};
  bool previousModeKnown{};
  bool previousEnabled{};
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

kstd::error_code Cirrus_DetectHardware(CirrusContext &ctx) {
  const uint8_t identity = ReadCrtc(cirrus_regs::kCrChipId);
  ctx.chipId = static_cast<uint8_t>((identity >> 2) & 0x3f);
  ctx.chip = cirrus_timing::IdentifyChip(identity);
  ctx.vramBytes = 0;
  if (ctx.chip == cirrus_timing::Chip::Unknown) {
    KE_Print("gfx_cirrus: unsupported Cirrus chip identity 0x{x}", ctx.chipId);
    return kernel::KernelError::Unsupported;
  }
  const uint8_t sr0f = ReadSequencer(cirrus_regs::kSrMemoryConfiguration);
  if (!cirrus_timing::DecodeVram(ctx.chip, sr0f, ctx.vramBytes)) {
    KE_Print("gfx_cirrus: chip 0x{x} reports an unknown SR0F memory encoding 0x{x}",
             ctx.chipId, sr0f);
    return kernel::KernelError::Unsupported;
  }
  return {};
}
// endregion

// region Mode selection
uint32_t BytesPerPixel(uint32_t bpp) { return bpp <= 8 ? 1 : (bpp <= 16 ? 2 : (bpp <= 24 ? 3 : 4)); }

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

kstd::error_code Cirrus_ProgramMode(CirrusContext *const ctx_cirrus) {
  const auto &timing = *ctx_cirrus->timing;
  const auto &depth = *ctx_cirrus->depth;

  if (!Cirrus_UnlockExtensions()) {
    KE_Print("gfx_cirrus: extension registers locked while setting the mode");
    return kernel::KernelError::DeviceError;
  }

  HAL_IO_Out8(cirrus_regs::kMiscOutputWrite, 0xEF);// colour, both sync signals negative

  // Standard sequencer setup for a packed-pixel mode: no reset, all planes
  // enabled, no chain-4 oddity.
  WriteSequencer(0x00, 0x03);
  WriteSequencer(0x01, 0x21);
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

  // Attribute controller: a straight-through palette; Enable opens both gates.
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
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x00);

  WriteHiddenDac(depth.hiddenDac);
  if (depth.bpp == 8) {
    Cirrus_ProgramPalette();
  }

  KE_Print("gfx_cirrus: programmed {}x{}x{} @ {}Hz, pitch {} bytes",
           ctx_cirrus->timing->width, ctx_cirrus->timing->height, ctx_cirrus->depth->bpp, ctx_cirrus->timing->refreshRate, ctx_cirrus->pitch);
  return kernel::KernelError::Success;
}
// endregion

// region Bank window and the region flush
// Returns the offset of `vramOffset` inside the 64 KiB window, moving the
// window only when that byte is outside the currently mapped view. A bank
// switch is expensive (emulators remap guest memory on every GR09 write),
// so the window is reused for as long as it covers the access.
uint32_t Cirrus_SetBank(CirrusContext *const ctx_cirrus, uint32_t vramOffset) {
  const uint32_t base = ctx_cirrus->activeBank * cirrus_regs::kBankGranularity;
  if (vramOffset >= base && vramOffset - base < cirrus_regs::kWindowSize) {
    return vramOffset - base;
  }
  const uint32_t bank = vramOffset / cirrus_regs::kBankGranularity;
  WriteGraphics(cirrus_regs::kGrOffset0, static_cast<uint8_t>(bank));
  ctx_cirrus->activeBank = bank;
  return vramOffset - bank * cirrus_regs::kBankGranularity;
}

// Aligned dword stores into the window; the source may be unaligned.
void Cirrus_CopyToWindow(volatile uint8_t *dst, const uint8_t *src, size_t length) {
  using UnalignedWord = uint32_t __attribute__((may_alias, aligned(1)));
  while (length && (reinterpret_cast<uintptr_t>(dst) & 3u)) {
    *dst++ = *src++;
    --length;
  }
  auto *dstWords = reinterpret_cast<volatile uint32_t *>(dst);
  for (; length >= 4; length -= 4, src += 4) {
    *dstWords++ = *reinterpret_cast<const UnalignedWord *>(src);
  }
  dst = reinterpret_cast<volatile uint8_t *>(dstWords);
  while (length--) {
    *dst++ = *src++;
  }
}

// Copies `length` bytes into VRAM through the bank window, switching banks
// whenever the run crosses the end of the currently mapped 64 KiB view.
void Cirrus_WriteVram(CirrusContext *const ctx_cirrus, uint32_t vramOffset, const uint8_t *src, size_t length) {
  auto *window = ctx_cirrus->window.as_ptr<volatile uint8_t>();
  while (length) {
    const uint32_t windowOffset = Cirrus_SetBank(ctx_cirrus, vramOffset);
    size_t chunk = cirrus_regs::kWindowSize - windowOffset;
    if (chunk > length) chunk = length;
    Cirrus_CopyToWindow(window + windowOffset, src, chunk);
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

// Copies one rectangle of the shadow buffer into VRAM with the CPU. The
// blitter's system-memory-source mode is not used: it has to be fed one
// byte per I/O-visible window write and was several times slower than
// these dword stores. Rows spanning the whole pitch are one contiguous run.
void Cirrus_FlushRect(CirrusContext *const ctx_cirrus, int32_t left, int32_t top, int32_t right, int32_t bottom) {
  if (left < 0) left = 0;
  if (top < 0) top = 0;
  if (right > static_cast<int32_t>(ctx_cirrus->timing->width)) right = static_cast<int32_t>(ctx_cirrus->timing->width);
  if (bottom > static_cast<int32_t>(ctx_cirrus->timing->height)) bottom = static_cast<int32_t>(ctx_cirrus->timing->height);
  if (right <= left || bottom <= top) return;

  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->depth->bpp);
  const uint32_t widthBytes = static_cast<uint32_t>(right - left) * bytesPerPixel;
  const auto height = static_cast<uint32_t>(bottom - top);
  const auto *shadow = ctx_cirrus->shadow.as_ptr<const uint8_t>();
  const size_t first = static_cast<size_t>(top) * ctx_cirrus->pitch + static_cast<size_t>(left) * bytesPerPixel;

  if (widthBytes == ctx_cirrus->pitch) {
    Cirrus_WriteVram(ctx_cirrus, static_cast<uint32_t>(first), shadow + first, static_cast<size_t>(widthBytes) * height);
    return;
  }
  for (uint32_t row = 0; row < height; ++row) {
    const size_t offset = first + static_cast<size_t>(row) * ctx_cirrus->pitch;
    Cirrus_WriteVram(ctx_cirrus, static_cast<uint32_t>(offset), shadow + offset, widthBytes);
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

// Unsupported asks the caller to use the software fallback; Timeout means the
// blit engine stalled and was reset.
kstd::error_code Cirrus_FillRect(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                                const kernel::KObjectPtr<kernel::KSurface> &dst, int x, int y, int w, int h, uint32_t colour) {
  if (!adapter) return kernel::KernelError::InvalidArgument;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) return kernel::KernelError::NotInitialized;

  if (!IsPrimary(ctx_cirrus, dst)) return kernel::KernelError::Unsupported;
  if (w <= 0 || h <= 0) return kernel::KernelError::InvalidSize;
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
  if (w <= 0 || h <= 0) return kernel::KernelError::Success;

  if (!Cirrus_WaitForBlitter()) {
    Cirrus_ResetBlitter();
    return kernel::KernelError::Timeout;
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
    return kernel::KernelError::Timeout;
  }
  WriteGraphics(cirrus_regs::kGrBltModeExtension, 0x00);

  // VRAM is already correct, so this produces no transfer: only the shadow
  // still needs the same pixels.
  Cirrus_FillShadow(ctx_cirrus, x, y, w, h, colour);
  return kernel::KernelError::Success;
}

kstd::error_code Cirrus_BitBlt(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                              const kernel::KObjectPtr<kernel::KSurface> &dst,
                              int dstX, int dstY,
                              const kernel::KObjectPtr<kernel::KSurface> &src,
                              int srcX, int srcY,
                              uint32_t width, uint32_t height,
                              kernel::RasterOp rop) {

  if (!adapter) return kernel::KernelError::InvalidArgument;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) return kernel::KernelError::NotInitialized;

  // Anything other than a straight copy, or a surface that isn't in VRAM,
  // is left to the kernel's software fallback.
  // Partially off-screen copies are also left to the clipping fallback.
  if (rop != kernel::RasterOp::SRCCOPY || !IsPrimary(ctx_cirrus, dst) || !IsPrimary(ctx_cirrus, src)) return kernel::KernelError::Unsupported;
  if (width == 0 || height == 0) return kernel::KernelError::Success;
  if (dstX < 0 || dstY < 0 || srcX < 0 || srcY < 0) return kernel::KernelError::Unsupported;
  if (dstX + static_cast<int>(width) > static_cast<int>(ctx_cirrus->timing->width) ||
      srcX + static_cast<int>(width) > static_cast<int>(ctx_cirrus->timing->width) ||
      dstY + static_cast<int>(height) > static_cast<int>(ctx_cirrus->timing->height) ||
      srcY + static_cast<int>(height) > static_cast<int>(ctx_cirrus->timing->height)) {
    return kernel::KernelError::Unsupported;
  }

  if (!Cirrus_WaitForBlitter()) {
    Cirrus_ResetBlitter();
    return kernel::KernelError::Timeout;
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
    return kernel::KernelError::Timeout;
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
  return kernel::KernelError::Success;
}

// The engine has no line primitive, so lines are walked in the shadow
// buffer; the swept area is covered by the caller's paint region, which
// reaches VRAM at EndPaint.
kstd::error_code Cirrus_DrawLine(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                                const kernel::KObjectPtr<kernel::KSurface> &dst, int x0, int y0, int x1, int y1, uint32_t colour) {
  if (!adapter) return kernel::KernelError::InvalidArgument;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) return kernel::KernelError::NotInitialized;

  if (!IsPrimary(ctx_cirrus, dst)) return kernel::KernelError::Unsupported;

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
  return kernel::KernelError::Success;
}
// endregion

// region KGraphicsAdapter / KDisplay hooks


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

struct CirrusModeCandidate {
  cirrus_regs::CirrusModeTiming timing{};
  cirrus_regs::CirrusDepth depth{};
  kernel::KDisplayModeInfo info{};
};

bool Cirrus_BuildMode(const kernel::KDisplay &display, const CirrusContext &ctx,
                      uint32_t width, uint32_t height, uint32_t refresh,
                      const cirrus_regs::CirrusDepth &depth,
                      CirrusModeCandidate &out) {
  if (ctx.chip == cirrus_timing::Chip::Unknown || !ctx.vramBytes ||
      !width || !height || !refresh)
    return false;
  cirrus_timing::GtfMode gtf{};
  if (!cirrus_timing::ComputeGtf(width, height, refresh, gtf) ||
      gtf.pixelClockHz > cirrus_timing::MaximumPixelClock(ctx.chip, depth.bpp))
    return false;

  cirrus_timing::ScanoutLayout layout{};
  if (!cirrus_timing::ComputeScanoutLayout(
          width, height, depth.bpp, gtf.pixelClockHz, ctx.vramBytes, layout, ctx.chip) ||
      layout.surfaceBytes > SIZE_MAX)
    return false;
  cirrus_timing::PllCandidate pll{};
  const uint32_t factor = static_cast<uint32_t>(layout.memoryClockHz / gtf.pixelClockHz);
  const uint32_t pixelLimit = cirrus_timing::MaximumPixelClock(ctx.chip, depth.bpp) * factor;
  const uint32_t hardwareLimit = cirrus_timing::MaximumHardwareClock(ctx.chip);
  const uint32_t pllLimit = ctx.chip == cirrus_timing::Chip::Gd5430 &&
                                   pixelLimit < hardwareLimit
                               ? pixelLimit : hardwareLimit;
  if (!cirrus_timing::ChoosePll(static_cast<uint32_t>(layout.memoryClockHz),
                                cirrus_timing::kMaximumPllErrorPpm, pll, pllLimit,
                                ctx.chip))
    return false;

  out = {};
  out.timing.width = width;
  out.timing.height = height;
  out.timing.refreshRate = refresh;
  out.timing.pixelClockHz = gtf.pixelClockHz;
  if (!cirrus_timing::PackCrtc(gtf, layout.pitch,
                               out.timing.crtc, out.timing.crtcExtended))
    return false;
  out.timing.vclkNumerator = pll.numerator;
  out.timing.vclkDenominator = pll.denominatorRegister;
  out.depth = depth;
  if (ctx.chip == cirrus_timing::Chip::Gd5430) {
    // Banked Alpine modes: no GD5434 low-memory double-VCLK mode or 32 bpp.
    switch (depth.bpp) {
      case 8: out.depth.sr07 = 0x01; out.depth.hiddenDac = 0; break;
      case 15: out.depth.sr07 = 0x07; out.depth.hiddenDac = 0xc0; break;
      case 16: out.depth.sr07 = 0x07; out.depth.hiddenDac = 0xc1; break;
      case 24: out.depth.sr07 = 0x05; out.depth.hiddenDac = 0xc5; break;
      default: return false;
    }
  }
  out.info.width = width;
  out.info.height = height;
  out.info.pitch = layout.pitch;
  out.info.videoMemoryBitmapWidth = width;
  out.info.videoMemoryBitmapHeight = height;
  out.info.bpp = depth.bpp;
  out.info.numberOfPlanes = 1;
  out.info.bitsPerPlane = depth.bpp;
  out.info.refreshRate = refresh;
  out.info.pixelFormat = depth.pixelFormat;
  Cirrus_FillColourFormat(depth, out.info);
  out.info.attributeFlags = kernel::DisplayModeFlags::GraphicsMode |
                            kernel::DisplayModeFlags::ColorMode |
                            kernel::DisplayModeFlags::HardwareAccelerated |
                            kernel::DisplayModeFlags::Banked;
  if (depth.pixelFormat == kernel::PixelFormat::INDEX8)
    out.info.attributeFlags |= kernel::DisplayModeFlags::PaletteDriven;
  return display.monitorCaps.Supports(out.info);
}

bool Cirrus_ModeAt(const kernel::KDisplay &display, const CirrusContext &ctx,
                   size_t requestedIndex, CirrusModeCandidate &out) {
  size_t index = 0;
  for (size_t timingIndex = 0; timingIndex < display.monitorCaps.count;
       ++timingIndex) {
    const auto &monitorTiming = display.monitorCaps.timings[timingIndex];
    const uint32_t refresh = monitorTiming.refreshRate
                                 ? monitorTiming.refreshRate
                                 : cirrus_regs::kFallbackRefreshRate;
    bool duplicate = false;
    for (size_t prior = 0; prior < timingIndex; ++prior) {
      const auto &earlier = display.monitorCaps.timings[prior];
      if (earlier.width == monitorTiming.width &&
          earlier.height == monitorTiming.height &&
          (earlier.refreshRate ? earlier.refreshRate
                              : cirrus_regs::kFallbackRefreshRate) == refresh) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;
    for (const auto &depth : cirrus_regs::kDepths) {
      if (!Cirrus_BuildMode(display, ctx, monitorTiming.width,
                            monitorTiming.height, refresh, depth, out))
        continue;
      if (index++ == requestedIndex) {
        out.info.modeIndex = index - 1;
        return true;
      }
    }
  }
  return false;
}

kstd::expected<kernel::KDisplayModeInfo, kstd::error_code> Cirrus_GetModeInfo(
    const kernel::KObjectPtr<kernel::KDisplay> &display, size_t modeIndex) {
  if (!display || !display->adapter)
    return kstd::unexpected(kernel::KernelError::InvalidArgument);
  const auto *ctx = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  CirrusModeCandidate candidate{};
  if (!ctx || !Cirrus_ModeAt(*display, *ctx, modeIndex, candidate))
    return kstd::unexpected(kernel::KernelError::OutOfRange);
  candidate.info.modeIndex = modeIndex;
  return candidate.info;
}

kstd::error_code Cirrus_Probe(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) {
  if (!adapter) {
    KE_Print("gfx_cirrus: No adapter to probe");
    return kernel::KernelError::InvalidArgument;
  }
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) {
    KE_Print("gfx_cirrus: No Cirrus context");
    return kernel::KernelError::NotInitialized;
  }
  if (ctx_cirrus->chip == cirrus_timing::Chip::Unknown || !ctx_cirrus->vramBytes)
    return kernel::KernelError::Unsupported;

  return kernel::KernelError::Success;
}

// The single head this adapter drives. Identity only: the entry points are
// bound later, from Cirrus_ConfigureDisplay.
kstd::error_code Cirrus_ProbeDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                                    const kernel::KGraphicsAdapterDisplayEnumInfo &enumInfo,
                                    kernel::KGraphicsAdapterDisplayInfo &outInfo) {
  if (enumInfo.displayIndex != 0) {
    return kernel::KernelError::OutOfRange;
  }

  outInfo.isPresent = true;
  outInfo.isMonitor = true;
  outInfo.isPnP = false;// The ISA Cirrus model exposes no DDC channel
  outInfo.edidSize = 0;
  outInfo.name = "VGA";
  return kernel::KernelError::Success;
}

// Reserve all software resources before the transaction can touch registers.
kstd::error_code Cirrus_PrepareMode(
    const kernel::KObjectPtr<kernel::KDisplay> &display, size_t modeIndex,
    kernel::KDisplayModeReservation &reservation) {
  if (!display || !display->adapter) return kernel::KernelError::InvalidArgument;
  const auto ctx_cirrus = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus || !ctx_cirrus->window) return kernel::KernelError::DeviceNotReady;

  CirrusModeCandidate candidate{};
  if (!Cirrus_ModeAt(*display, *ctx_cirrus, modeIndex, candidate))
    return kernel::KernelError::OutOfRange;
  const uint32_t pitch = candidate.info.pitch;
  const size_t size = static_cast<size_t>(pitch) * candidate.info.height;
  void *buffer = KE_Malloc(size);
  if (!buffer) {
    KE_Print("gfx_cirrus: {}KiB shadow for {}x{}x{} could not be allocated, keeping the current mode",
             size / 1024, candidate.info.width, candidate.info.height,
             candidate.info.bpp);
    return kernel::KernelError::OutOfMemory;
  }
  __builtin_memset(buffer, 0, size);

  auto backing = KE_VMM_CreateDirectBacking(kernel::VirtualAddress::from_ptr(buffer), size);
  if (!backing) {
    KE_Free(buffer);
    KE_Print("gfx_cirrus: could not allocate mode backing, keeping the current mode: {}", backing.error().message());
    return backing.error();
  }

  auto *data = static_cast<CirrusReservationData *>(KE_Malloc(sizeof(CirrusReservationData)));
  if (!data) {
    KE_Free(buffer);
    return kernel::KernelError::OutOfMemory;
  }
  *data = CirrusReservationData{
      .candidateShadow = kernel::VirtualAddress::from_ptr(buffer),
      .previousShadow = ctx_cirrus->shadow,
      .previousPitch = ctx_cirrus->pitch,
      .candidateTiming = candidate.timing,
      .candidateDepth = candidate.depth,
      .previousTiming = ctx_cirrus->timing ? *ctx_cirrus->timing
                                           : cirrus_regs::CirrusModeTiming{},
      .previousDepth = ctx_cirrus->depth ? *ctx_cirrus->depth
                                         : cirrus_regs::CirrusDepth{},
      .previousTimingValid = ctx_cirrus->timing != nullptr,
      .previousDepthValid = ctx_cirrus->depth != nullptr,
      .previousModeIndex = ctx_cirrus->modeIndex,
      .previousModeKnown = ctx_cirrus->modeKnown,
      .previousEnabled = ctx_cirrus->enabled};

  reservation = {};
  reservation.modeIndex = modeIndex;
  reservation.mode = candidate.info;
  reservation.binding.backing = kstd::move(*backing);
  reservation.binding.type = kernel::SurfaceType::Framebuffer;
  reservation.binding.flags = kernel::SurfaceFlags::TopDown | kernel::SurfaceFlags::Linear;
  reservation.driverData = data;
  return kernel::KernelError::Success;
}

kstd::error_code Cirrus_ProgramReservedMode(
    const kernel::KObjectPtr<kernel::KDisplay> &display,
    kernel::KDisplayModeReservation &reservation) {
  if (!display || !display->adapter || !reservation.driverData)
    return kernel::KernelError::InvalidArgument;
  auto *ctx = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx)
    return kernel::KernelError::DeviceNotReady;
  auto *data = static_cast<CirrusReservationData *>(reservation.driverData);
  ctx->shadow = data->candidateShadow;
  ctx->pitch = reservation.mode.pitch;
  ctx->activeTiming = data->candidateTiming;
  ctx->activeDepth = data->candidateDepth;
  ctx->timing = &ctx->activeTiming;
  ctx->depth = &ctx->activeDepth;
  if (const auto error = Cirrus_ProgramMode(ctx); error) {
    ctx->shadow = data->previousShadow;
    ctx->pitch = data->previousPitch;
    if (data->previousTimingValid) {
      ctx->activeTiming = data->previousTiming;
      ctx->timing = &ctx->activeTiming;
    } else {
      ctx->timing = nullptr;
    }
    if (data->previousDepthValid) {
      ctx->activeDepth = data->previousDepth;
      ctx->depth = &ctx->activeDepth;
    } else {
      ctx->depth = nullptr;
    }
    return error;
  }
  Cirrus_FlushScreen(ctx);
  ctx->modeKnown = true;
  ctx->modeIndex = reservation.modeIndex;
  ctx->enabled = false;
  return kernel::KernelError::Success;
}

kstd::error_code Cirrus_RestoreMode(
    const kernel::KObjectPtr<kernel::KDisplay> &display, size_t modeIndex,
    kernel::KDisplayModeReservation &reservation) {
  if (!display || !display->adapter || !reservation.driverData)
    return kernel::KernelError::InvalidArgument;
  auto *ctx = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  auto *data = static_cast<CirrusReservationData *>(reservation.driverData);
  if (!ctx || !data->previousModeKnown || data->previousModeIndex != modeIndex ||
      !data->previousTimingValid || !data->previousDepthValid)
    return kernel::KernelError::Unsupported;
  ctx->shadow = data->previousShadow;
  ctx->pitch = data->previousPitch;
  ctx->activeTiming = data->previousTiming;
  ctx->activeDepth = data->previousDepth;
  ctx->timing = &ctx->activeTiming;
  ctx->depth = &ctx->activeDepth;
  if (const auto error = Cirrus_ProgramMode(ctx); error) {
    ctx->modeKnown = false;
    ctx->enabled = false;
    return error;
  }
  Cirrus_FlushScreen(ctx);
  ctx->modeKnown = true;
  ctx->modeIndex = modeIndex;
  ctx->enabled = false;
  return kernel::KernelError::Success;
}

void Cirrus_FinishMode(const kernel::KObjectPtr<kernel::KDisplay> &display,
                       kernel::KDisplayModeReservation &reservation,
                       bool committed) {
  if (!reservation.driverData) return;
  auto *data = static_cast<CirrusReservationData *>(reservation.driverData);
  auto *ctx = display && display->adapter
      ? display->adapter->GetDeviceExtensionAs<CirrusContext>()
      : nullptr;
  if (committed) {
    if (data->previousShadow && data->previousShadow != data->candidateShadow)
      KE_Free(data->previousShadow.as_ptr<void>());
  } else if (!ctx || ctx->shadow != data->candidateShadow) {
    KE_Free(data->candidateShadow.as_ptr<void>());
  }
  KE_Free(data);
  reservation.driverData = nullptr;
}

kstd::expected<kernel::KDisplayActualState, kstd::error_code>
Cirrus_QueryActualState(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display || !display->adapter)
    return kstd::unexpected(kernel::KernelError::InvalidArgument);
  const auto *ctx = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx) return kstd::unexpected(kernel::KernelError::DeviceNotReady);
  return kernel::KDisplayActualState{
      .known = ctx->modeKnown,
      .enabled = ctx->enabled,
      .modeIndex = ctx->modeIndex};
}

// Video output is gated by the sequencer's screen-off bit (SR01 bit 5):
// set blanks the display without disturbing the programmed timing.
kstd::error_code Cirrus_Enable(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display || !display->adapter) return kernel::KernelError::InvalidArgument;
  auto *ctx = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx) return kernel::KernelError::DeviceNotReady;
  HAL_IO_In8(cirrus_regs::kInputStatus1);
  HAL_IO_Out8(cirrus_regs::kAttributeControllerIndexData, 0x20);
  WriteSequencer(0x01, static_cast<uint8_t>(ReadSequencer(0x01) & ~cirrus_regs::kSr01ScreenOff));
  ctx->enabled = true;
  return kernel::KernelError::Success;
}

kstd::error_code Cirrus_Disable(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display || !display->adapter) return kernel::KernelError::InvalidArgument;
  auto *ctx = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx) return kernel::KernelError::DeviceNotReady;
  WriteSequencer(0x01, static_cast<uint8_t>(ReadSequencer(0x01) | cirrus_regs::kSr01ScreenOff));
  ctx->enabled = false;
  return kernel::KernelError::Success;
}

kstd::error_code Cirrus_Validate(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display || !display->adapter || !display->isPresent)
    return kernel::KernelError::NotFound;
  const auto *ctx = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  return ctx && ctx->chip != cirrus_timing::Chip::Unknown && ctx->vramBytes && ctx->window
             ? kernel::KernelError::Success
             : kernel::KernelError::DeviceNotReady;
}

// Binds this driver's per-head entry points on the display the graphics
// subsystem created for the head Cirrus_ProbeDisplay reported.
kstd::error_code Cirrus_ConfigureDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/, const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display) {
    return kernel::KernelError::InvalidArgument;
  }

  display->GetModeInfo = Cirrus_GetModeInfo;
  display->PrepareMode = Cirrus_PrepareMode;
  display->ProgramMode = Cirrus_ProgramReservedMode;
  display->FinishMode = Cirrus_FinishMode;
  display->RestoreMode = Cirrus_RestoreMode;
  display->QueryActualState = Cirrus_QueryActualState;
  display->Enable = Cirrus_Enable;
  display->Disable = Cirrus_Disable;
  display->Validate = Cirrus_Validate;
  return kernel::KernelError::Success;
}
// endregion

kstd::error_code Cirrus_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  // The generic ISA presence check also sees standard VGA at these ports.
  // Reject it before PnP reserves the shared VGA aperture for this driver,
  // otherwise the real VGA fallback cannot acquire its exclusive resources.
  if (!Cirrus_UnlockExtensions()) {
    KE_Print("gfx_cirrus: no Cirrus chip detected (SR06 unlock failed)");
    return kernel::KernelError::NotFound;
  }
  const auto fdo = KE_IO_CreateDeviceWithContext<CirrusContext>(driver, "Cirrus", kernel::device_type::Video);
  if (!fdo) {
    KE_Print("gfx_cirrus: Failed to create Cirrus device: {}", fdo.error().message());
    return fdo.error();
  }

  if (const auto attached = KE_IO_AttachFilterDevice(*fdo, pdo); !attached) {
    KE_Print("gfx_cirrus: Failed to attach Cirrus device: {}", attached.error().message());
    return attached.error();
  }
  return kernel::KernelError::Success;
}

kstd::error_code Cirrus_DeviceControl(const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t code,
                                      kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred) {
  (void) input;
  transferred = 0;

  const auto ctx_cirrus = device->deviceExtension.as<CirrusContext>();

  if (code != IoControlCode_DisplayQueryMode) return kernel::KernelError::Unsupported;
  if (output.size() < sizeof(IoctlDisplayMode)) return kernel::KernelError::BufferTooSmall;
  if (!ctx_cirrus) return kernel::KernelError::NotInitialized;
  // No mode has been programmed yet: there is nothing to report.
  if (!ctx_cirrus->shadow || !ctx_cirrus->depth) return kernel::KernelError::DeviceNotReady;

  const IoctlDisplayMode mode{.width = ctx_cirrus->timing->width, .height = ctx_cirrus->timing->height, .bpp = ctx_cirrus->depth->bpp, .pitch = ctx_cirrus->pitch};
  __builtin_memcpy(output.data(), &mode, sizeof(mode));
  transferred = sizeof(mode);
  KE_Print("gfx_cirrus: DeviceControl QueryMode -> {}x{}x{}", mode.width, mode.height, mode.bpp);
  return kernel::KernelError::Success;
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
  if (windowRes->length < cirrus_regs::kWindowSize ||
      windowRes->length % kPageSize) {
    KE_Print("gfx_cirrus: assigned bank aperture is too small or not page aligned");
    KE_RES_ReleaseResources(pdo);
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
  if (Cirrus_DetectHardware(*ctx_cirrus)) {
    KE_RES_ReleaseResources(pdo);
    return;
  }

  auto &as = KE_VMM_GetKernelAddressSpace();
  const auto region = KE_VMM_AllocateRegion(as, windowRes->length, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!region) {
    KE_Print("gfx_cirrus: Failed to reserve virtual address space for the bank window: {}", region.error().message());
    return;
  }

  const auto virt = kernel::VirtualAddress::from_ptr(*region);
  const size_t pageCount = windowRes->length / kPageSize;
  for (size_t i = 0; i < pageCount; ++i) {
    if (const auto mapped = KE_VMM_MapPhysical(
            as,
            virt + (i * kPageSize),
            kernel::PhysicalAddress(windowRes->start) + (i * kPageSize),
            kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
        mapped) {
      KE_Print("gfx_cirrus: Failed to map the bank window: {}", mapped.message());
      return;
    }
  }
  ctx_cirrus->window = virt;
  ctx_cirrus->activeBank = 0;

  KE_Print("gfx_cirrus: {} id 0x{x}, SR0F-decoded {}KiB, {}KiB aperture mapped",
           ctx_cirrus->chip == cirrus_timing::Chip::Gd5430 ? "GD5430" : "GD5434",
           ctx_cirrus->chipId, ctx_cirrus->vramBytes / 1024,
           windowRes->length / 1024);

  auto adapter = KE_GFX_RegisterGraphicsAdapter(driver, fdo, "Cirrus");
  if (!adapter) {
    KE_Print("gfx_cirrus: Failed to register graphics adapter: {}", adapter.error().message());
    return;
  }
  ctx_cirrus->adapter = kstd::move(*adapter);

  // Mode buffers are reserved only after the topology manager validates a
  // path and begins its target transaction.
  ctx_cirrus->adapter->Probe = Cirrus_Probe;
  ctx_cirrus->adapter->ProbeDisplay = Cirrus_ProbeDisplay;
  ctx_cirrus->adapter->ConfigureDisplay = Cirrus_ConfigureDisplay;
  ctx_cirrus->adapter->FillRect = Cirrus_FillRect;
  ctx_cirrus->adapter->DrawLine = Cirrus_DrawLine;
  ctx_cirrus->adapter->BitBlt = Cirrus_BitBlt;
  ctx_cirrus->adapter->Present = Cirrus_Present;
  ctx_cirrus->adapter->caps = kernel::KGfxCaps::HwFill | kernel::KGfxCaps::HwBlt | kernel::KGfxCaps::VisibleFill;
}
}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = Cirrus_AddDevice;
  driver->StartDevice = Cirrus_StartDevice;
  driver->DeviceControl = Cirrus_DeviceControl;
}
