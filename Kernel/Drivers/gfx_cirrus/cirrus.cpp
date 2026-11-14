
#include <Kernel.hpp>
#include <stdkrn.hpp>

#include <kernel/gfx/video_present.hpp>
#include <kernel/pci.hpp>

#include "cirrus_registers.hpp"
#include "cirrus_timing.hpp"

#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__struct_IoctlDisplayMode.h>

namespace {

constexpr size_t kPageSize = 4096;

struct CirrusModeCatalogEntry;

struct CirrusModeCatalogKey {
  uint8_t chipId{};
  cirrus_timing::Chip chip{cirrus_timing::Chip::Unknown};
  uint32_t vramBytes{};
  uint32_t resourceGeneration{};
  kernel::KMonitorCapabilities monitorCaps{};
};


// One offscreen VRAM allocation (Step 13). `surface` is identity only: GFX
// calls DestroySurface before the surface is freed, which clears the entry.
struct CirrusVramBlock {
  kernel::KSurface *surface{};
  uint32_t offset{};
  uint32_t size{};
};
constexpr size_t kMaxVramBlocks = 32;

// Per-instance state, held in the function device object's extension (see
// Cirrus_AddDevice). Everything below `shadow` is only valid once a mode has
// actually been programmed; before the first mode transaction the shadow is null.
// No dirty-rectangle state: the USER layer tells Present what to transfer.
struct CirrusContext {
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
  volatile uint8_t *bltMmio{};// BitBLT MMIO block (BAR1+0x100); null = port I/O
  bool modeKnown{false};
  bool enabled{false};
  size_t modeIndex{};
  CirrusModeCatalogEntry *modeCatalog{};
  size_t modeCatalogCount{};
  CirrusModeCatalogKey modeCatalogKey{};
  bool modeCatalogKeyValid{};
  // Offscreen VRAM starts after the scanout area of the active mode.
  uint32_t vramBase{};
  CirrusVramBlock vramBlocks[kMaxVramBlocks]{};
  // PCI binding only (bus_pci answered GetDeviceInfo). ISA bindings keep
  // isPci false, lfb/mmio null, and use the banked window and port I/O.
  bool isPci{false};
  kernel::pci::DeviceInfo pciInfo{};
  kernel::VirtualAddress lfb; // mapped BAR0 linear framebuffer
  uint32_t lfbBytes{};
  kernel::VirtualAddress mmio;// mapped BAR1 registers (BitBLT at +0x100)
  uint32_t mmioBytes{};
  uint8_t savedSr17{};
  bool sr17Changed{false};

  ~CirrusContext();
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

// Memory-mapped BitBLT registers (GD5436 and later), set once a PCI binding
// maps BAR1. Null means port I/O. The block starts at BAR1+0x100; this
// layout follows QEMU's cirrus_vga.c (CIRRUS_MMIO_*), which models the
// GD5446. The pointer lives in CirrusContext::bltMmio.

// Returns the MMIO offset of a BLT-related GR register, or -1.
int BltMmioOffset(uint8_t gr) {
  switch (gr) {
    case 0x00: return 0x00; case 0x10: return 0x01; case 0x12: return 0x02; case 0x14: return 0x03;
    case 0x01: return 0x04; case 0x11: return 0x05; case 0x13: return 0x06; case 0x15: return 0x07;
    case cirrus_regs::kGrBltStatus: return 0x40;
    case cirrus_regs::kGrBltRop: return 0x1A;
    case cirrus_regs::kGrBltModeExtension: return 0x1B;
    default: return gr >= 0x20 && gr <= 0x30 ? gr - 0x20 + 0x08 : -1;
  }
}

void WriteBlt(const CirrusContext *const ctx_cirrus, uint8_t index, uint8_t value) {
  if (ctx_cirrus->bltMmio) {
    if (const int offset = BltMmioOffset(index); offset >= 0) {
      ctx_cirrus->bltMmio[offset] = value;
      return;
    }
  }
  WriteGraphics(index, value);
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
  const bool needsSr17 = ctx.chip == cirrus_timing::Chip::Gd5446 ||
                         ctx.chip == cirrus_timing::Chip::Gd5480;
  const uint8_t sr17 = needsSr17 ? ReadSequencer(0x17) : 0;
  if (!cirrus_timing::DecodeVram(ctx.chip, sr0f, ctx.vramBytes, sr17)) {
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

  if (ctx_cirrus->lfb) {
    // PCI linear framebuffer: as Xorg alp_driver.c AlpModeInit, keep SR07
    // bits 7:5, set bit 4 (linear addressing) and the depth bits.
    const auto before = ReadSequencer(cirrus_regs::kSrExtendedSequencerMode);
    const auto sr07 = static_cast<uint8_t>((before & 0xE0) | 0x10 | depth.sr07);
    WriteSequencer(cirrus_regs::kSrExtendedSequencerMode, sr07);
    KE_Print("gfx_cirrus: SR07 0x{:x} -> 0x{:x} (linear), read back 0x{:x}", before, sr07,
             ReadSequencer(cirrus_regs::kSrExtendedSequencerMode));
  } else {
    WriteSequencer(cirrus_regs::kSrExtendedSequencerMode, depth.sr07);
  }
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
  if (ctx_cirrus->lfb && vramOffset <= ctx_cirrus->lfbBytes && length <= ctx_cirrus->lfbBytes - vramOffset) {
    // PCI linear framebuffer: no GR09 bank switching.
    Cirrus_CopyToWindow(ctx_cirrus->lfb.as_ptr<volatile uint8_t>() + vramOffset, src, length);
    return;
  }
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

void Cirrus_ReadVram(CirrusContext *const ctx_cirrus, uint32_t vramOffset, uint8_t *dst, size_t length) {
  if (ctx_cirrus->lfb && vramOffset <= ctx_cirrus->lfbBytes && length <= ctx_cirrus->lfbBytes - vramOffset) {
    const auto *lfb = ctx_cirrus->lfb.as_ptr<volatile uint8_t>() + vramOffset;
    for (size_t i = 0; i < length; ++i) dst[i] = lfb[i];
    return;
  }
  const auto *window = ctx_cirrus->window.as_ptr<volatile uint8_t>();
  while (length) {
    const uint32_t windowOffset = Cirrus_SetBank(ctx_cirrus, vramOffset);
    size_t chunk = cirrus_regs::kWindowSize - windowOffset;
    if (chunk > length) chunk = length;
    for (size_t i = 0; i < chunk; ++i) dst[i] = window[windowOffset + i];
    vramOffset += static_cast<uint32_t>(chunk);
    dst += chunk;
    length -= chunk;
  }
}

// Aborts whatever the engine is doing and disarms it. The reset is only
// acted on when the bit falls again, so both writes are required: leaving
// the engine armed would make it swallow every later window write.
void Cirrus_ResetBlitter(const CirrusContext *const ctx_cirrus) {
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltStatus, cirrus_regs::kBltStatusReset);
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltStatus, 0x00);
}

// The engine must be idle before it is reprogrammed. Returns false on
// timeout, in which case the caller takes the CPU path instead.
bool Cirrus_WaitForBlitter(const CirrusContext *const ctx_cirrus) {
  for (uint32_t spin = 0; spin < 100000; ++spin) {
    uint8_t status;
    if (ctx_cirrus->bltMmio) {
      status = ctx_cirrus->bltMmio[0x40];
    } else {
      HAL_IO_Out8(cirrus_regs::kGraphicsControllerIndex, cirrus_regs::kGrBltStatus);
      status = HAL_IO_In8(cirrus_regs::kGraphicsControllerData);
    }
    if ((status & cirrus_regs::kBltStatusBusy) == 0) {
      return true;
    }
  }
  return false;
}

void Cirrus_ProgramBlitGeometry(const CirrusContext *const ctx_cirrus, uint32_t widthBytes, uint32_t height, uint32_t dstPitch, uint32_t srcPitch, uint32_t dstAddress) {
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltWidthLow, static_cast<uint8_t>((widthBytes - 1) & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltWidthHigh, static_cast<uint8_t>(((widthBytes - 1) >> 8) & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltHeightLow, static_cast<uint8_t>((height - 1) & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltHeightHigh, static_cast<uint8_t>(((height - 1) >> 8) & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltDstPitchLow, static_cast<uint8_t>(dstPitch & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltDstPitchHigh, static_cast<uint8_t>((dstPitch >> 8) & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltSrcPitchLow, static_cast<uint8_t>(srcPitch & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltSrcPitchHigh, static_cast<uint8_t>((srcPitch >> 8) & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltDstAddrLow, static_cast<uint8_t>(dstAddress & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltDstAddrMid, static_cast<uint8_t>((dstAddress >> 8) & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltDstAddrHigh, static_cast<uint8_t>((dstAddress >> 16) & 0x3F));
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

// Where a surface of this adapter lives in VRAM: the primary at offset 0,
// offscreen surfaces in their allocated block. False for anything else.
struct CirrusPlacement {
  uint32_t offset{};
  uint32_t pitch{};
  uint32_t width{};
  uint32_t height{};
  bool primary{};
};

bool Cirrus_Locate(CirrusContext *const ctx_cirrus, const kernel::KObjectPtr<kernel::KSurface> &surface,
                   CirrusPlacement &out) {
  if (!ctx_cirrus->shadow || !ctx_cirrus->depth || !surface) return false;
  if (IsPrimary(ctx_cirrus, surface)) {
    out = {.offset = 0, .pitch = ctx_cirrus->pitch, .width = ctx_cirrus->timing->width,
           .height = ctx_cirrus->timing->height, .primary = true};
    return true;
  }
  if (surface->flags & kernel::SurfaceFlags::Lost) return false;
  for (const auto &block: ctx_cirrus->vramBlocks) {
    if (block.surface == surface.get()) {
      out = {.offset = block.offset, .pitch = static_cast<uint32_t>(surface->pitch),
             .width = surface->width, .height = surface->height, .primary = false};
      return true;
    }
  }
  return false;
}

// A mode change reuses VRAM: every offscreen surface is lost and its block freed.
void Cirrus_LoseOffscreen(CirrusContext *const ctx_cirrus) {
  for (auto &block: ctx_cirrus->vramBlocks) {
    if (block.surface) block.surface->flags |= kernel::SurfaceFlags::Lost;
    block = {};
  }
  const uint32_t scanout = ctx_cirrus->timing ? ctx_cirrus->pitch * ctx_cirrus->timing->height : 0;
  ctx_cirrus->vramBase = (scanout + 255u) & ~255u;
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
    WriteBlt(ctx_cirrus, kColourRegisters[i], bytes[i]);
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

  CirrusPlacement place{};
  if (!Cirrus_Locate(ctx_cirrus, dst, place)) return kernel::KernelError::Unsupported;
  if (w <= 0 || h <= 0) return kernel::KernelError::InvalidSize;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > static_cast<int>(place.width)) w = static_cast<int>(place.width) - x;
  if (y + h > static_cast<int>(place.height)) h = static_cast<int>(place.height) - y;
  if (w <= 0 || h <= 0) return kernel::KernelError::Success;

  if (!Cirrus_WaitForBlitter(ctx_cirrus)) {
    Cirrus_ResetBlitter(ctx_cirrus);
    return kernel::KernelError::Timeout;
  }

  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->depth->bpp);
  const uint32_t widthBytes = static_cast<uint32_t>(w) * bytesPerPixel;
  const uint32_t dstAddress = place.offset + static_cast<uint32_t>(y) * place.pitch + static_cast<uint32_t>(x) * bytesPerPixel;

  Cirrus_ProgramFillColour(ctx_cirrus, colour);
  Cirrus_ProgramBlitGeometry(ctx_cirrus, widthBytes, static_cast<uint32_t>(h), place.pitch, 0, dstAddress);
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltRop, cirrus_regs::kBltRopSrcCopy);
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltModeExtension, cirrus_regs::kBltModeExtSolidFill);
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltMode,
                static_cast<uint8_t>(cirrus_regs::kBltModePatternCopy | cirrus_regs::kBltModeColourExpand |
                                     ctx_cirrus->depth->bltPixelWidth));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltStatus, cirrus_regs::kBltStatusStart);

  if (!Cirrus_WaitForBlitter(ctx_cirrus)) {
    Cirrus_ResetBlitter(ctx_cirrus);
    WriteBlt(ctx_cirrus, cirrus_regs::kGrBltModeExtension, 0x00);
    return kernel::KernelError::Timeout;
  }
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltModeExtension, 0x00);

  // VRAM is already correct, so this produces no transfer: only the shadow
  // still needs the same pixels. Offscreen surfaces are VRAM-only.
  if (place.primary) Cirrus_FillShadow(ctx_cirrus, x, y, w, h, colour);
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
  CirrusPlacement dp{}, sp{};
  if (rop != kernel::RasterOp::SRCCOPY || !Cirrus_Locate(ctx_cirrus, dst, dp) || !Cirrus_Locate(ctx_cirrus, src, sp))
    return kernel::KernelError::Unsupported;
  if (width == 0 || height == 0) return kernel::KernelError::Success;
  if (dstX < 0 || dstY < 0 || srcX < 0 || srcY < 0) return kernel::KernelError::Unsupported;
  if (dstX + static_cast<int>(width) > static_cast<int>(dp.width) ||
      srcX + static_cast<int>(width) > static_cast<int>(sp.width) ||
      dstY + static_cast<int>(height) > static_cast<int>(dp.height) ||
      srcY + static_cast<int>(height) > static_cast<int>(sp.height)) {
    return kernel::KernelError::Unsupported;
  }

  if (!Cirrus_WaitForBlitter(ctx_cirrus)) {
    Cirrus_ResetBlitter(ctx_cirrus);
    return kernel::KernelError::Timeout;
  }

  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->depth->bpp);
  const uint32_t widthBytes = width * bytesPerPixel;
  const uint32_t dstStart = dp.offset + static_cast<uint32_t>(dstY) * dp.pitch + static_cast<uint32_t>(dstX) * bytesPerPixel;
  const uint32_t srcStart = sp.offset + static_cast<uint32_t>(srcY) * sp.pitch + static_cast<uint32_t>(srcX) * bytesPerPixel;
  uint32_t dstAddress = dstStart;
  uint32_t srcAddress = srcStart;

  // Overlapping rectangles moving forwards would overwrite source rows
  // before they are read, so the engine walks them backwards instead.
  // Distinct surfaces occupy disjoint VRAM and never overlap.
  const bool same = dst.get() == src.get();
  const bool backwards = same && dstAddress > srcAddress &&
                         dstAddress < srcAddress + static_cast<uint32_t>(height) * dp.pitch;
  if (backwards) {
    dstAddress += (height - 1) * dp.pitch + widthBytes - 1;
    srcAddress += (height - 1) * sp.pitch + widthBytes - 1;
  }

  Cirrus_ProgramBlitGeometry(ctx_cirrus, widthBytes, height, dp.pitch, sp.pitch, dstAddress);
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltSrcAddrLow, static_cast<uint8_t>(srcAddress & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltSrcAddrMid, static_cast<uint8_t>((srcAddress >> 8) & 0xFF));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltSrcAddrHigh, static_cast<uint8_t>((srcAddress >> 16) & 0x3F));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltRop, cirrus_regs::kBltRopSrcCopy);
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltModeExtension, 0x00);
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltMode,
                static_cast<uint8_t>(ctx_cirrus->depth->bltPixelWidth | (backwards ? cirrus_regs::kBltModeBackwards : 0)));
  WriteBlt(ctx_cirrus, cirrus_regs::kGrBltStatus, cirrus_regs::kBltStatusStart);

  if (!Cirrus_WaitForBlitter(ctx_cirrus)) {
    Cirrus_ResetBlitter(ctx_cirrus);
    return kernel::KernelError::Timeout;
  }

  if (!dp.primary) return kernel::KernelError::Success;// offscreen: VRAM-only
  auto *shadow = ctx_cirrus->shadow.as_ptr<uint8_t>();
  if (!sp.primary) {
    // Offscreen -> screen: the shadow can only learn the pixels from VRAM.
    // Without this, the next Present would overwrite them with stale shadow.
    for (uint32_t row = 0; row < height; ++row) {
      const uint32_t at = dstStart + row * dp.pitch;
      Cirrus_ReadVram(ctx_cirrus, at, shadow + at, widthBytes);
    }
    return kernel::KernelError::Success;
  }

  // Mirror the copy into the shadow buffer, row by row, so both copies of
  // the screen agree.
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

void Cirrus_DestroySurface(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter, kernel::KSurface *surface);

// Frees its VRAM block when the last reference goes, so the block table
// never names a freed surface. `adapter` is set by GFX right after creation.
struct CirrusVramSurface final : kernel::KSurface {
  using KSurface::KSurface;
  ~CirrusVramSurface() override { Cirrus_DestroySurface(adapter, this); }
};

// Offscreen surfaces (Step 13): first fit in a fixed block table after the
// scanout area. Only the active depth is supported (the blitter's pixel
// width is per mode). VRAM-only: no backing, no shadow.
kstd::error_code Cirrus_CreateSurface(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter, uint32_t width,
                                      uint32_t height, kernel::PixelFormat format,
                                      kernel::KObjectPtr<kernel::KSurface> &out) {
  if (!adapter) return kernel::KernelError::InvalidArgument;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus || !ctx_cirrus->shadow || !ctx_cirrus->depth) return kernel::KernelError::Unsupported;
  if (format != ctx_cirrus->depth->pixelFormat) return kernel::KernelError::Unsupported;
  if (!width || !height || width > 4096 || height > 4096) return kernel::KernelError::InvalidSize;

  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->depth->bpp);
  const uint32_t pitch = (width * bytesPerPixel + 7u) & ~7u;
  if (pitch > 0x1FFF) return kernel::KernelError::Unsupported;
  const uint32_t size = (pitch * height + 255u) & ~255u;

  CirrusVramBlock *slot = nullptr;
  for (auto &block: ctx_cirrus->vramBlocks)
    if (!block.surface) {
      slot = &block;
      break;
    }
  if (!slot) return kernel::KernelError::OutOfMemory;

  // Lowest free offset that overlaps no live block.
  uint32_t offset = ctx_cirrus->vramBase;
  for (bool moved = true; moved;) {
    moved = false;
    for (const auto &block: ctx_cirrus->vramBlocks) {
      if (block.surface && offset < block.offset + block.size && block.offset < offset + size) {
        offset = block.offset + block.size;
        moved = true;
      }
    }
  }
  if (offset + size > ctx_cirrus->vramBytes || offset + size < offset) return kernel::KernelError::OutOfMemory;

  auto surface = kernel::CreateKObject<CirrusVramSurface>("CirrusVram");
  if (!surface) return surface.error();
  auto &s = **surface;
  s.adapter = adapter;
  s.type = kernel::SurfaceType::OffscreenBitmap;
  s.width = width;
  s.height = height;
  s.pitch = static_cast<int32_t>(pitch);
  s.bpp = ctx_cirrus->depth->bpp;
  s.sizeInBytes = size;
  s.pixelFormat = format;
  s.flags = kernel::SurfaceFlags::TopDown | kernel::SurfaceFlags::Linear;
  *slot = {.surface = surface->get(), .offset = offset, .size = size};
  out = kstd::move(*surface);
  return kernel::KernelError::Success;
}

void Cirrus_DestroySurface(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter, kernel::KSurface *surface) {
  if (!adapter || !surface) return;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) return;
  for (auto &block: ctx_cirrus->vramBlocks)
    if (block.surface == surface) block = {};
}

// VRAM is only reachable through the 64 KiB bank window: no linear CPU mapping.
kstd::error_code Cirrus_Map(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &, kernel::KSurface *, void *&,
                            uint32_t &) {
  return kernel::KernelError::Unsupported;
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

struct CirrusModeCatalogEntry {
  bool exposed{};
  CirrusModeCandidate candidate{};
};

struct CirrusPllSeed {
  cirrus_timing::Chip chip;
  uint32_t width;
  uint32_t height;
  uint32_t bpp;
  uint32_t memoryClockFactor;
  int8_t vramProfile;
  uint8_t numerator;
  uint8_t rawDivider;
  uint8_t postDivider;
};

#define CIRRUS_PLL_SEED(chip_, width_, height_, bpp_, factor_, profile_, n_, d_, p_) \
  {chip_, width_, height_, bpp_, factor_, profile_, n_, d_, p_}

constexpr cirrus_timing::GtfMode kCommonGtfSeeds[] = {
    {640, 480, 60, 656, 720, 800, 481, 484, 497, 23855673},
    {800, 600, 60, 832, 912, 1024, 601, 604, 622, 38216085},
    {1024, 768, 60, 1080, 1184, 1344, 769, 772, 795, 64109903},
};

constexpr CirrusPllSeed kCommonPllSeeds[] = {
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5430, 640, 480, 8, 1, -1, 40, 12, 1),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5430, 640, 480, 15, 1, -1, 40, 12, 1),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5430, 640, 480, 16, 1, -1, 40, 12, 1),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5430, 640, 480, 24, 3, -1, 50, 10, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5430, 800, 600, 8, 1, -1, 32, 12, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5430, 800, 600, 15, 1, -1, 32, 12, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5430, 800, 600, 16, 1, -1, 32, 12, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5430, 1024, 768, 8, 1, -1, 103, 23, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 640, 480, 8, 1, -1, 35, 21, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 640, 480, 15, 1, 1, 35, 21, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 640, 480, 16, 1, 1, 35, 21, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 640, 480, 15, 2, 0, 40, 12, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 640, 480, 16, 2, 0, 40, 12, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 640, 480, 24, 3, -1, 35, 7, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 640, 480, 32, 4, -1, 60, 9, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 800, 600, 8, 1, -1, 32, 12, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 800, 600, 15, 1, 1, 32, 12, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 800, 600, 16, 1, 1, 32, 12, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 800, 600, 15, 2, 0, 48, 9, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 800, 600, 16, 2, 0, 48, 9, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 800, 600, 24, 3, -1, 56, 7, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 1024, 768, 8, 1, -1, 103, 23, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 1024, 768, 15, 1, -1, 103, 23, 0),
    CIRRUS_PLL_SEED(cirrus_timing::Chip::Gd5434, 1024, 768, 16, 1, -1, 103, 23, 0),
};

#undef CIRRUS_PLL_SEED

const cirrus_timing::GtfMode *Cirrus_FindGtfSeed(
    uint32_t width, uint32_t height, uint32_t refresh) {
  if (refresh != 60) return nullptr;
  for (const auto &seed: kCommonGtfSeeds)
    if (seed.width == width && seed.height == height) return &seed;
  return nullptr;
}

const CirrusPllSeed *Cirrus_FindPllSeed(
    const CirrusContext &ctx, uint32_t width, uint32_t height, uint32_t bpp,
    uint32_t memoryClockFactor) {
  const int8_t profile =
      ctx.chip == cirrus_timing::Chip::Gd5434 &&
              (width == 640 || width == 800) &&
              (bpp == 15 || bpp == 16)
          ? (ctx.vramBytes <= 1024 * 1024 ? 0 : 1)
          : -1;
  for (const auto &seed: kCommonPllSeeds)
    if (seed.chip == ctx.chip && seed.width == width &&
        seed.height == height && seed.bpp == bpp &&
        seed.memoryClockFactor == memoryClockFactor &&
        seed.vramProfile == profile)
      return &seed;
  return nullptr;
}

bool Cirrus_ValidatePllSeed(const CirrusPllSeed &seed, uint64_t targetClockHz,
                            uint32_t maximumClockHz,
                            cirrus_timing::PllCandidate &out) {
  const bool alpine = seed.chip == cirrus_timing::Chip::Gd5430;
  if ((!alpine && seed.chip != cirrus_timing::Chip::Gd5434) ||
      seed.numerator < (alpine ? 16 : 32) ||
      seed.numerator > (alpine ? 126 : 127) ||
      seed.rawDivider < (alpine ? 10 : 7) || seed.rawDivider > 31 ||
      seed.postDivider > 1 ||
      (!alpine && seed.postDivider && seed.rawDivider < 16) ||
      !targetClockHz || targetClockHz > UINT32_MAX ||
      targetClockHz > maximumClockHz)
    return false;

  const uint32_t denominator =
      static_cast<uint32_t>(seed.rawDivider) * (1 + seed.postDivider);
  if (!denominator || denominator > UINT8_MAX) return false;

  const uint64_t outputNumerator =
      uint64_t{cirrus_timing::kReferenceClockHz} * seed.numerator;
  if (alpine &&
      (outputNumerator < uint64_t{28636000} * seed.rawDivider ||
       outputNumerator > uint64_t{111000000} * seed.rawDivider))
    return false;
  if (outputNumerator > uint64_t{maximumClockHz} * denominator)
    return false;

  if (targetClockHz > UINT64_MAX / denominator) return false;
  const uint64_t requestedNumerator = targetClockHz * denominator;
  const uint64_t error = outputNumerator > requestedNumerator
                             ? outputNumerator - requestedNumerator
                             : requestedNumerator - outputNumerator;
  if (error >
      (UINT64_MAX - requestedNumerator / 2) / uint64_t{1000000})
    return false;
  const uint64_t errorPpm =
      (error * 1000000 + requestedNumerator / 2) / requestedNumerator;
  if (errorPpm > cirrus_timing::kMaximumPllErrorPpm) return false;

  out = cirrus_timing::PllCandidate{
      .numerator = seed.numerator,
      .denominator = static_cast<uint8_t>(denominator),
      .denominatorRegister = static_cast<uint8_t>(
          (alpine ? 0 : 0x80) | (seed.rawDivider << 1) | seed.postDivider),
      .errorPpm = static_cast<uint32_t>(errorPpm)};
  return true;
}

bool Cirrus_ComputeGtf(uint32_t width, uint32_t height, uint32_t refresh,
                       cirrus_timing::GtfMode &out) {
  return cirrus_timing::ComputeGtf(width, height, refresh, out);
}

bool Cirrus_ChoosePll(uint32_t targetClockHz, uint32_t maximumErrorPpm,
                      cirrus_timing::PllCandidate &out, uint32_t maximumClockHz,
                      cirrus_timing::Chip chip) {
  return cirrus_timing::ChoosePll(targetClockHz, maximumErrorPpm, out,
                                  maximumClockHz, chip);
}

bool Cirrus_BuildMode(const CirrusContext &ctx,
                      uint32_t width, uint32_t height, uint32_t refresh,
                      const cirrus_regs::CirrusDepth &depth,
                      const kernel::KMonitorCapabilities &monitorCaps,
                      CirrusModeCandidate &out) {
  if (ctx.chip == cirrus_timing::Chip::Unknown || !ctx.vramBytes ||
      !width || !height || !refresh)
    return false;
  cirrus_timing::GtfMode gtf{};
  const auto *timingSeed = Cirrus_FindGtfSeed(width, height, refresh);
  if (timingSeed) {
    gtf = *timingSeed;
  } else if (!Cirrus_ComputeGtf(width, height, refresh, gtf)) {
    return false;
  }
  if (gtf.pixelClockHz >
      cirrus_timing::MaximumPixelClock(ctx.chip, depth.bpp))
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
                                ? pixelLimit
                                : hardwareLimit;
  const auto *pllSeed = timingSeed
                            ? Cirrus_FindPllSeed(ctx, width, height, depth.bpp, factor)
                            : nullptr;
  if (pllSeed) {
    if (!Cirrus_ValidatePllSeed(*pllSeed, layout.memoryClockHz, pllLimit, pll))
      return false;
  } else if (!Cirrus_ChoosePll(static_cast<uint32_t>(layout.memoryClockHz),
                               cirrus_timing::kMaximumPllErrorPpm, pll, pllLimit,
                               ctx.chip)) {
    return false;
  }

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
      case 8:
        out.depth.sr07 = 0x01;
        out.depth.hiddenDac = 0;
        break;
      case 15:
        out.depth.sr07 = 0x07;
        out.depth.hiddenDac = 0xc0;
        break;
      case 16:
        out.depth.sr07 = 0x07;
        out.depth.hiddenDac = 0xc1;
        break;
      case 24:
        out.depth.sr07 = 0x05;
        out.depth.hiddenDac = 0xc5;
        break;
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
  return monitorCaps.Supports(out.info);
}

bool Cirrus_SameCatalogKey(const CirrusModeCatalogKey &left,
                           const CirrusModeCatalogKey &right) {
  if (left.chipId != right.chipId || left.chip != right.chip ||
      left.vramBytes != right.vramBytes ||
      left.resourceGeneration != right.resourceGeneration ||
      left.monitorCaps.count != right.monitorCaps.count ||
      left.monitorCaps.maxWidth != right.monitorCaps.maxWidth ||
      left.monitorCaps.maxHeight != right.monitorCaps.maxHeight ||
      left.monitorCaps.maxRefreshRate != right.monitorCaps.maxRefreshRate ||
      left.monitorCaps.isDefault != right.monitorCaps.isDefault)
    return false;
  for (size_t i = 0; i < kernel::KMonitorCapabilities::MaxTimings; ++i) {
    const auto &a = left.monitorCaps.timings[i];
    const auto &b = right.monitorCaps.timings[i];
    if (a.width != b.width || a.height != b.height ||
        a.refreshRate != b.refreshRate)
      return false;
  }
  return true;
}

CirrusModeCatalogKey Cirrus_CurrentCatalogKey(
    const kernel::KDisplay &display, const CirrusContext &ctx) {
  return CirrusModeCatalogKey{
      .chipId = ctx.chipId,
      .chip = ctx.chip,
      .vramBytes = ctx.vramBytes,
      .resourceGeneration = display.adapter->resourceGeneration,
      .monitorCaps = display.monitorCaps};
}

void *Cirrus_AllocateCatalog(size_t bytes) {
  return KE_Malloc(bytes);
}

kstd::error_code Cirrus_EnsureModeCatalog(const kernel::KDisplay &display,
                                          CirrusContext &ctx) {
  const auto key = Cirrus_CurrentCatalogKey(display, ctx);
  if (ctx.modeCatalogKeyValid &&
      Cirrus_SameCatalogKey(ctx.modeCatalogKey, key))
    return kernel::KernelError::Success;
  if (key.monitorCaps.count > kernel::KMonitorCapabilities::MaxTimings)
    return kernel::KernelError::InvalidArgument;

  constexpr size_t depthCount = sizeof(cirrus_regs::kDepths) /
                                sizeof(cirrus_regs::kDepths[0]);
  if (kernel::KMonitorCapabilities::MaxTimings > SIZE_MAX / depthCount)
    return kernel::KernelError::OutOfMemory;
  constexpr size_t maxEntries =
      kernel::KMonitorCapabilities::MaxTimings * depthCount;
  if (maxEntries > SIZE_MAX / sizeof(CirrusModeCatalogEntry))
    return kernel::KernelError::OutOfMemory;
  const size_t bytes = maxEntries * sizeof(CirrusModeCatalogEntry);
  auto *staging =
      static_cast<CirrusModeCatalogEntry *>(Cirrus_AllocateCatalog(bytes));
  if (!staging) return kernel::KernelError::OutOfMemory;

  size_t count = 0;
  for (size_t timingIndex = 0; timingIndex < key.monitorCaps.count;
       ++timingIndex) {
    const auto &monitorTiming = key.monitorCaps.timings[timingIndex];
    const uint32_t refresh = monitorTiming.refreshRate
                                 ? monitorTiming.refreshRate
                                 : cirrus_regs::kFallbackRefreshRate;
    bool duplicate = false;
    for (size_t prior = 0; prior < timingIndex; ++prior) {
      const auto &earlier = key.monitorCaps.timings[prior];
      if (earlier.width == monitorTiming.width &&
          earlier.height == monitorTiming.height &&
          (earlier.refreshRate ? earlier.refreshRate
                               : cirrus_regs::kFallbackRefreshRate) == refresh) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;
    for (const auto &depth: cirrus_regs::kDepths) {
      auto &entry = staging[count++];
      entry = {};
      entry.exposed = Cirrus_BuildMode(
          ctx, monitorTiming.width, monitorTiming.height, refresh,
          depth, key.monitorCaps, entry.candidate);
    }
  }

  if (!Cirrus_SameCatalogKey(key, Cirrus_CurrentCatalogKey(display, ctx))) {
    KE_Free(staging);
    return kernel::KernelError::Retry;
  }
  auto *retired = ctx.modeCatalog;
  ctx.modeCatalog = staging;
  ctx.modeCatalogCount = count;
  ctx.modeCatalogKey = key;
  ctx.modeCatalogKeyValid = true;
  KE_Free(retired);
  return kernel::KernelError::Success;
}

kstd::expected<CirrusModeCandidate, kstd::error_code> Cirrus_ModeAt(
    const kernel::KDisplay &display, CirrusContext &ctx, size_t requestedIndex) {
  if (const auto error = Cirrus_EnsureModeCatalog(display, ctx); error)
    return kstd::unexpected(error);
  size_t index = 0;
  for (size_t i = 0; i < ctx.modeCatalogCount; ++i) {
    const auto &entry = ctx.modeCatalog[i];
    if (!entry.exposed) continue;
    if (index++ == requestedIndex) {
      auto candidate = entry.candidate;
      candidate.info.modeIndex = index - 1;
      return candidate;
    }
  }
  return kstd::unexpected(kernel::KernelError::OutOfRange);
}

kstd::expected<kernel::KDisplayModeInfo, kstd::error_code> Cirrus_GetModeInfo(
    const kernel::KObjectPtr<kernel::KDisplay> &display, size_t modeIndex) {
  if (!display || !display->adapter)
    return kstd::unexpected(kernel::KernelError::InvalidArgument);
  auto *ctx = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx) return kstd::unexpected(kernel::KernelError::DeviceNotReady);
  auto candidate = Cirrus_ModeAt(*display, *ctx, modeIndex);
  if (!candidate) return kstd::unexpected(candidate.error());
  candidate->info.modeIndex = modeIndex;
  return candidate->info;
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
  auto *ctx_cirrus = display->adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus || !ctx_cirrus->window) return kernel::KernelError::DeviceNotReady;

  auto result = Cirrus_ModeAt(*display, *ctx_cirrus, modeIndex);
  if (!result) return result.error();
  const auto &candidate = *result;
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
  Cirrus_LoseOffscreen(ctx);
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
    Cirrus_LoseOffscreen(ctx);
    return error;
  }
  Cirrus_LoseOffscreen(ctx);
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

// Detects a bus_pci binding and logs every bus_pci query, as an exercise of
// the PCI bus driver. Any failure means a non-PCI (ISA/legacy) binding, and
// the context is left untouched so the existing paths run unchanged.
void Cirrus_ProbePciBus(const kernel::KDevicePtr<kernel::KDevice> &pdo, CirrusContext &ctx) {
  kernel::pci::DeviceInfo info{};
  if (const auto status = kernel::pci::GetDeviceInfo(pdo, info); status) {
    KE_Print("gfx_cirrus: GetDeviceInfo -> {}: not a PCI binding, using ISA paths", status.message());
    return;
  }
  if (info.vendorId != 0x1013) {
    KE_Print("gfx_cirrus: GetDeviceInfo returned vendor 0x{x}, not Cirrus: using ISA paths", info.vendorId);
    return;
  }
  ctx.isPci = true;
  ctx.pciInfo = info;
  KE_Print("gfx_cirrus: PCI {}:{}.{} {x:04}:{x:04} rev {x:02} BAR0 0x{x}/0x{x} BAR1 0x{x}/0x{x}",
           info.bus, info.dev, info.fn, info.vendorId, info.deviceId, info.revision,
           info.bars[0].base, info.bars[0].size, info.bars[1].base, info.bars[1].size);
}

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
  (*fdo)->deviceExtension.destroy = [](void *ptr) {
    static_cast<CirrusContext *>(ptr)->~CirrusContext();
  };

  if (const auto attached = KE_IO_AttachFilterDevice(*fdo, pdo); !attached) {
    KE_Print("gfx_cirrus: Failed to attach Cirrus device: {}", attached.error().message());
    return attached.error();
  }
  // The bus queries must run here: after AddDevice, PnP rebinds
  // pdo->driverObject to this driver and they no longer reach bus_pci.
  if (auto *ctx = (*fdo)->deviceExtension.as<CirrusContext>()) Cirrus_ProbePciBus(pdo, *ctx);
  return kernel::KernelError::Success;
}

// Unmaps and releases a range mapped by Cirrus_MapPhysicalRange.
void Cirrus_UnmapPhysicalRange(kernel::VirtualAddress virt, size_t length, const char *what) {
  if (!virt) return;
  for (size_t i = 0; i < length / kPageSize; ++i) {
    HAL_VMM_UnmapPage(virt + (i * kPageSize));
    HAL_VMM_Flush(virt + (i * kPageSize));
  }
  if (const auto freed = KE_VMM_FreeRegion(KE_VMM_GetKernelAddressSpace(), virt.as_ptr()); freed)
    KE_Print("gfx_cirrus: cannot release {} region: {}", what, freed.message());
}

CirrusContext::~CirrusContext() {
  KE_Free(modeCatalog);
  if (sr17Changed) WriteSequencer(0x17, savedSr17);
  bltMmio = nullptr;
  Cirrus_UnmapPhysicalRange(mmio, mmioBytes, "BAR1 MMIO registers");
  Cirrus_UnmapPhysicalRange(lfb, lfbBytes, "BAR0 linear framebuffer");
}

// Maps `length` bytes of device memory (page aligned) read/write and
// uncached into the kernel address space: 486/Pentium have no MTRRs to force
// PCI apertures uncached, and BitBLT status polling must see the device.
// Returns a null address on failure.
kernel::VirtualAddress Cirrus_MapPhysicalRange(uint32_t physical, size_t length, const char *what) {
  constexpr auto kFlags = kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE |
                          kernel::vmm::ProtectFlags::NOCACHE;
  auto &as = KE_VMM_GetKernelAddressSpace();
  const auto region = KE_VMM_AllocateRegion(as, length, kFlags);
  if (!region) {
    KE_Print("gfx_cirrus: cannot reserve address space for {}: {}", what, region.error().message());
    return {};
  }
  const auto virt = kernel::VirtualAddress::from_ptr(*region);
  for (size_t i = 0; i < length / kPageSize; ++i) {
    if (const auto mapped = KE_VMM_MapPhysical(as, virt + (i * kPageSize), kernel::PhysicalAddress(physical) + (i * kPageSize),
                                               kFlags);
        mapped) {
      KE_Print("gfx_cirrus: cannot map {}: {}", what, mapped.message());
      // Undo the pages already mapped, then release the reservation.
      for (size_t j = 0; j < i; ++j) {
        HAL_VMM_UnmapPage(virt + (j * kPageSize));
        HAL_VMM_Flush(virt + (j * kPageSize));
      }
      if (const auto freed = KE_VMM_FreeRegion(as, virt.as_ptr()); freed)
        KE_Print("gfx_cirrus: cannot release {} region: {}", what, freed.message());
      return {};
    }
  }
  return virt;
}

// PCI only: maps BAR0 as the linear framebuffer and BAR1 as the MMIO
// register block. Any failure leaves the banked window / port I/O in use.
void Cirrus_MapPciApertures(CirrusContext &ctx) {
  const auto &info = ctx.pciInfo;
  const auto &bar0 = info.bars[0];
  if (info.barCount > 0 && !bar0.io && bar0.base && bar0.size) {
    // Only the first vramBytes of BAR0 are plain linear VRAM (higher
    // quarters select byte-swapping apertures on the GD5446).
    uint32_t length = ctx.vramBytes < bar0.size ? ctx.vramBytes : bar0.size;
    length &= ~uint32_t{kPageSize - 1};
    if (length) {
      ctx.lfb = Cirrus_MapPhysicalRange(bar0.base, length, "BAR0 linear framebuffer");
      if (ctx.lfb) ctx.lfbBytes = length;
    }
  }
  KE_Print("gfx_cirrus: PCI linear framebuffer: {}", ctx.lfb ? "BAR0 mapped" : "unavailable, using the banked window");

  const auto &bar1 = info.bars[1];
  if (info.barCount > 1 && !bar1.io && bar1.base && bar1.size >= 0x200) {
    ctx.mmio = Cirrus_MapPhysicalRange(bar1.base, kPageSize, "BAR1 MMIO registers");
    if (ctx.mmio) {
      // SR17 bit 2 enables memory-mapped I/O, bit 6 selects the PCI BAR
      // instead of the 0xB8000 window (Cirrus GD5446 TRM; QEMU maps the
      // legacy window only for bits == 0x04). NOT_YET_VERIFIED on hardware.
      ctx.mmioBytes = kPageSize;
      ctx.savedSr17 = ReadSequencer(0x17);
      ctx.sr17Changed = true;
      WriteSequencer(0x17, static_cast<uint8_t>(ctx.savedSr17 | 0x44));
      ctx.bltMmio = ctx.mmio.as_ptr<volatile uint8_t>() + 0x100;
    }
  }
  KE_Print("gfx_cirrus: BitBLT registers: {}", ctx.bltMmio ? "MMIO (BAR1+0x100)" : "port I/O");
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
  if (ctx_cirrus->isPci) Cirrus_MapPciApertures(*ctx_cirrus);

  KE_Print("gfx_cirrus: {} id 0x{x}, SR0F-decoded {}KiB, {}KiB aperture mapped",
           cirrus_timing::ChipName(ctx_cirrus->chip),
           ctx_cirrus->chipId, ctx_cirrus->vramBytes / 1024,
           windowRes->length / 1024);

  auto adapter = KE_GFX_RegisterGraphicsAdapter(driver, fdo, "Cirrus");
  if (!adapter) {
    KE_Print("gfx_cirrus: Failed to register graphics adapter: {}", adapter.error().message());
    return;
  }
  const auto graphicsAdapter = kstd::move(*adapter);

  // Mode buffers are reserved only after the topology manager validates a
  // path and begins its target transaction.
  graphicsAdapter->Probe = Cirrus_Probe;
  graphicsAdapter->ProbeDisplay = Cirrus_ProbeDisplay;
  graphicsAdapter->ConfigureDisplay = Cirrus_ConfigureDisplay;
  graphicsAdapter->FillRect = Cirrus_FillRect;
  graphicsAdapter->DrawLine = Cirrus_DrawLine;
  graphicsAdapter->CreateSurface = Cirrus_CreateSurface;
  graphicsAdapter->DestroySurface = Cirrus_DestroySurface;
  graphicsAdapter->Map = Cirrus_Map;
  graphicsAdapter->BitBlt = Cirrus_BitBlt;
  graphicsAdapter->Present = Cirrus_Present;
  graphicsAdapter->caps = kernel::KGfxCaps::HwFill | kernel::KGfxCaps::HwBlt | kernel::KGfxCaps::VisibleFill;
}
}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = Cirrus_AddDevice;
  driver->StartDevice = Cirrus_StartDevice;
  driver->DeviceControl = Cirrus_DeviceControl;
}
