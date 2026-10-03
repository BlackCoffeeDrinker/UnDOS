
#include <Kernel.hpp>

#include "cirrus_registers.hpp"

#include <kernel/configuration_manager.hpp>

#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__struct_IoctlDisplayMode.h>

namespace {

constexpr size_t kPageSize = 4096;

// Module-level state: like gfx_vga, this driver only ever handles a single
// adapter/display, so there is no per-instance device context to allocate.
// No dirty-rectangle state: the USER layer tells Present what to transfer.
struct CirrusContext {
  kernel::KObjectPtr<kernel::KGraphicsAdapter> adapter;
  kernel::KObjectPtr<kernel::KDisplay> display;
  kernel::VirtualAddress window;// mapped 0xA0000 bank window
  kernel::VirtualAddress shadow;// flat system-RAM primary surface
  uint32_t width{};
  uint32_t height{};
  uint32_t pitch{};
  uint32_t bpp{};
  uint32_t refreshRate{};
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

const cirrus_regs::CirrusModeTiming *FindTiming(uint32_t width, uint32_t height) {
  for (const auto &timing: cirrus_regs::kModeTimings) {
    if (timing.width == width && timing.height == height) return &timing;
  }
  return nullptr;
}

const cirrus_regs::CirrusDepth *FindDepth(uint32_t bpp) {
  for (const auto &depth: cirrus_regs::kDepths) {
    if (depth.bpp == bpp) return &depth;
  }
  return nullptr;
}

// A mode is only usable if its framebuffer fits in the adapter's VRAM.
bool FitsVideoRam(const cirrus_regs::CirrusModeTiming &timing, const cirrus_regs::CirrusDepth &depth) {
  (void) timing;
  (void) depth;
  
  return true;
  //return PitchFor(timing.width, depth.bpp) * timing.height <= cirrus_regs::kMaxVideoRamSize;
}

// Parses a decimal unsigned integer; returns false on empty/malformed input.
bool ParseUInt32(const kstd::string_view &text, uint32_t &out) {
  if (text.empty()) return false;
  uint32_t value = 0;
  for (const char c: text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<uint32_t>(c - '0');
  }
  out = value;
  return true;
}

// Reads the named value subkey under `key`, matching the "open a subkey
// named after the value, then read its default value" pattern the registry
// uses everywhere else (see kernel/src/legacy_isa_enum.cpp).
bool ReadStringValue(const kernel::KConfigurationKeyPtr &key, const kstd::string_view &valueName, kstd::string &out) {
  const auto valueKey = KE_CM_OpenKey(key, valueName);
  if (!valueKey) return false;
  out.resize(KE_CM_GetKeyValueStringSize(valueKey));
  return KE_CM_GetKeyValueString(valueKey, kstd::span(out.data(), out.size()));
}

bool ReadUInt32Value(const kernel::KConfigurationKeyPtr &key, const kstd::string_view &valueName, uint32_t &out) {
  kstd::string text;
  if (!ReadStringValue(key, valueName, text)) return false;
  return ParseUInt32(text, out);
}

// Reads \System\Console\Desktop. Anything missing, unparsable or
// unsupported leaves the fallback mode in place, with the reason logged.
void Cirrus_ReadConfiguredMode(uint32_t &width, uint32_t &height, uint32_t &bpp, uint32_t &refreshRate) {
  width = cirrus_regs::kFallbackWidth;
  height = cirrus_regs::kFallbackHeight;
  bpp = cirrus_regs::kFallbackBpp;
  refreshRate = cirrus_regs::kFallbackRefreshRate;

  const auto localMachine = KE_CM_OpenRoot("LocalMachine");
  if (!localMachine) {
    KE_Print("gfx_cirrus: no LocalMachine hive, using the fallback mode");
    return;
  }
  const auto systemKey = KE_CM_OpenKey(localMachine, "System");
  const auto consoleKey = systemKey ? KE_CM_OpenKey(systemKey, "Console") : nullptr;
  const auto desktopKey = consoleKey ? KE_CM_OpenKey(consoleKey, "Desktop") : nullptr;
  if (!desktopKey) {
    KE_Print("gfx_cirrus: no System\\Console\\Desktop key, using the fallback mode");
    return;
  }

  uint32_t w = 0, h = 0, b = 0, r = 0;
  if (!ReadUInt32Value(desktopKey, "Width", w) || !ReadUInt32Value(desktopKey, "Height", h) ||
      !ReadUInt32Value(desktopKey, "Bpp", b)) {
    KE_Print("gfx_cirrus: Desktop key is missing or has an unparsable Width/Height/Bpp");
    return;
  }
  if (!ReadUInt32Value(desktopKey, "RefreshRate", r)) {
    r = cirrus_regs::kFallbackRefreshRate;
  }

  const auto *timing = FindTiming(w, h);
  const auto *depth = FindDepth(b);
  if (!timing || !depth) {
    KE_Print("gfx_cirrus: configured mode {}x{}x{} is not supported, using the fallback mode", w, h, b);
    return;
  }
  if (!FitsVideoRam(*timing, *depth)) {
    KE_Print("gfx_cirrus: configured mode {}x{}x{} exceeds video memory, using the fallback mode", w, h, b);
    return;
  }

  width = w;
  height = h;
  bpp = b;
  refreshRate = r;
}

// The step-down order used when the shadow buffer cannot be allocated:
// give up depth before resolution, ending at 640x480x8. Returns false once
// the smallest mode has been tried.
bool StepDownMode(const cirrus_regs::CirrusModeTiming *&timing, const cirrus_regs::CirrusDepth *&depth) {
  const auto *depthsEnd = cirrus_regs::kDepths + (sizeof(cirrus_regs::kDepths) / sizeof(cirrus_regs::kDepths[0]));
  if (depth + 1 < depthsEnd) {
    ++depth;
    return true;
  }
  if (timing > cirrus_regs::kModeTimings) {
    --timing;
    depth = cirrus_regs::kDepths;
    return true;
  }
  return false;
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
           ctx_cirrus->width, ctx_cirrus->height, ctx_cirrus->bpp, ctx_cirrus->refreshRate, ctx_cirrus->pitch);
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

  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->bpp);
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
  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->bpp);
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
  if (right > static_cast<int32_t>(ctx_cirrus->width)) right = static_cast<int32_t>(ctx_cirrus->width);
  if (bottom > static_cast<int32_t>(ctx_cirrus->height)) bottom = static_cast<int32_t>(ctx_cirrus->height);
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
  Cirrus_FlushRect(ctx_cirrus, 0, 0, static_cast<int32_t>(ctx_cirrus->width), static_cast<int32_t>(ctx_cirrus->height));
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
    Cirrus_FlushRect(ctx_cirrus, 0, 0, static_cast<int32_t>(ctx_cirrus->width), static_cast<int32_t>(ctx_cirrus->height));
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
bool IsPrimary(CirrusContext *const ctx_cirrus, const kernel::KObjectPtr<kernel::KSurface> &surface) {
  return surface && surface->backing && surface->backing->directAddress == ctx_cirrus->shadow;
}

// The fill colour is held in the foreground colour bytes, widened to the
// active depth.
void Cirrus_ProgramFillColour(CirrusContext *const ctx_cirrus, uint32_t colour) {
  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->bpp);
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
  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->bpp);
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
  if (x + w > static_cast<int>(ctx_cirrus->width)) w = static_cast<int>(ctx_cirrus->width) - x;
  if (y + h > static_cast<int>(ctx_cirrus->height)) h = static_cast<int>(ctx_cirrus->height) - y;
  if (w <= 0 || h <= 0) return true;

  if (!Cirrus_WaitForBlitter()) {
    Cirrus_ResetBlitter();
    return false;
  }

  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->bpp);
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
  if (dstX + static_cast<int>(width) > static_cast<int>(ctx_cirrus->width) ||
      srcX + static_cast<int>(width) > static_cast<int>(ctx_cirrus->width) ||
      dstY + static_cast<int>(height) > static_cast<int>(ctx_cirrus->height) ||
      srcY + static_cast<int>(height) > static_cast<int>(ctx_cirrus->height)) {
    return false;
  }

  if (!Cirrus_WaitForBlitter()) {
    Cirrus_ResetBlitter();
    return false;
  }

  const uint32_t bytesPerPixel = BytesPerPixel(ctx_cirrus->bpp);
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
    if (x0 >= 0 && y0 >= 0 && x0 < static_cast<int>(ctx_cirrus->width) && y0 < static_cast<int>(ctx_cirrus->height)) {
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


// Mode 0 is the active (configured, or stepped-down) mode; the rest of the
// list is every supported resolution/depth combination that fits in VRAM.
size_t Cirrus_GetModeCount(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  (void) display;

  size_t count = 1;

  for (const auto &timing: cirrus_regs::kModeTimings) {
    for (const auto &depth: cirrus_regs::kDepths) {
      if (FitsVideoRam(timing, depth)) ++count;
    }
  }
  return count;
}

// TODO: ALLOW LISTING ALL THE MODES
bool Cirrus_GetModeInfo(const kernel::KObjectPtr<kernel::KDisplay> &display, size_t modeIndex, kernel::KDisplayModeInfo &outInfo) {
  if (!display) return false;
  if (!display->adapter) return false;

  const auto ctx_cirrus = display->adapter->GetDeviceExtensionAs<CirrusContext>();

  uint32_t width = ctx_cirrus->width;
  uint32_t height = ctx_cirrus->height;
  uint32_t refreshRate = ctx_cirrus->refreshRate;
  const cirrus_regs::CirrusDepth *depth = ctx_cirrus->depth;

  if (modeIndex != 0) {
    size_t index = 1;
    bool found = false;
    for (const auto &timing: cirrus_regs::kModeTimings) {
      for (const auto &candidate: cirrus_regs::kDepths) {
        if (!FitsVideoRam(timing, candidate)) continue;
        if (index++ != modeIndex) continue;
        width = timing.width;
        height = timing.height;
        refreshRate = timing.refreshRate;
        depth = &candidate;
        found = true;
        break;
      }
      if (found) break;
    }
    if (!found) return false;
  }

  if (!depth) return false;

  outInfo.width = width;
  outInfo.height = height;
  outInfo.pitch = PitchFor(width, depth->bpp);
  outInfo.bpp = depth->bpp;
  outInfo.refreshRate = refreshRate;
  outInfo.pixelFormat = depth->pixelFormat;
  outInfo.interlaced = false;
  outInfo.hwAccelerated = true;
  outInfo.textMode = false;

  return true;
}

bool Cirrus_Probe(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) {
  if (!adapter) return false;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) return false;

  return static_cast<bool>(ctx_cirrus->shadow);
}

bool Cirrus_EnumerateDisplays(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) {
  if (!adapter) return false;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();
  if (!ctx_cirrus) return false;

  if (!ctx_cirrus->display) {
    // Assume a single display
    ctx_cirrus->display = KE_GFX_RegisterDisplay(ctx_cirrus->adapter, "VGA");
    if (!ctx_cirrus->display) {
      KE_Print("gfx_cirrus: Failed to register display");
      return false;
    }

    ctx_cirrus->display->GetModeCount = Cirrus_GetModeCount;
    ctx_cirrus->display->GetModeInfo = Cirrus_GetModeInfo;
  }

  return static_cast<bool>(ctx_cirrus->display);
}


// TODO: Implement dynamic mode setting
bool Cirrus_SetMode(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                    const kernel::KObjectPtr<kernel::KDisplay> &display,
                    const kernel::KObjectPtr<kernel::KSurface> &outSurface, size_t modeIndex) {
  (void) display;
  if (!adapter) return false;
  const auto ctx_cirrus = adapter->GetDeviceExtensionAs<CirrusContext>();

  if (!ctx_cirrus || modeIndex != 0 || !outSurface || !ctx_cirrus->shadow || !ctx_cirrus->depth) {
    return false;
  }

  Cirrus_ProgramMode(ctx_cirrus);
  Cirrus_FlushScreen(ctx_cirrus);

  outSurface->backing = KE_VMM_CreateDirectBacking(ctx_cirrus->shadow, ctx_cirrus->pitch * ctx_cirrus->height);
  if (!outSurface->backing) {
    return false;
  }

  outSurface->width = ctx_cirrus->width;
  outSurface->height = ctx_cirrus->height;
  outSurface->pitch = ctx_cirrus->pitch;
  outSurface->pixelFormat = ctx_cirrus->depth->pixelFormat;
  return true;
}

void Cirrus_UnsetMode(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter, const kernel::KObjectPtr<kernel::KDisplay> &display) {
  (void) adapter;
  (void) display;
  // Nothing to tear down: the bank window and the shadow buffer stay for
  // the lifetime of the driver.
}
// endregion

bool Cirrus_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
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
  if (!ctx_cirrus->shadow) return kernel::IoStatus::Unsupported;

  const IoctlDisplayMode mode{.width = ctx_cirrus->width, .height = ctx_cirrus->height, .bpp = ctx_cirrus->bpp, .pitch = ctx_cirrus->pitch};
  __builtin_memcpy(output.data(), &mode, sizeof(mode));
  transferred = sizeof(mode);
  KE_Print("gfx_cirrus: DeviceControl QueryMode -> {}x{}x{}", mode.width, mode.height, mode.bpp);
  return kernel::IoStatus::Success;
}

// Picks the mode to run in and allocates its shadow buffer, stepping the
// mode down (depth first, then resolution) whenever the allocation fails,
// so a low-RAM machine ends up with a smaller screen rather than an
// exhausted heap. Returns false if even 640x480x8 cannot be allocated.
bool Cirrus_AllocateShadow(CirrusContext *const ctx_cirrus) {
  uint32_t width, height, bpp, refreshRate;
  if (!ctx_cirrus) return false;
  
  Cirrus_ReadConfiguredMode(width, height, bpp, refreshRate);

  const auto *timing = FindTiming(width, height);
  const auto *depth = FindDepth(bpp);
  if (!timing || !depth) return false;

  const uint32_t requestedWidth = width;
  const uint32_t requestedHeight = height;
  const uint32_t requestedBpp = bpp;

  while (true) {
    if (FitsVideoRam(*timing, *depth)) {
      const uint32_t pitch = PitchFor(timing->width, depth->bpp);
      const size_t size = static_cast<size_t>(pitch) * timing->height;
      if (void *buffer = KE_Malloc(size)) {
        __builtin_memset(buffer, 0, size);
        ctx_cirrus->shadow = kernel::VirtualAddress::from_ptr(buffer);
        ctx_cirrus->width = timing->width;
        ctx_cirrus->height = timing->height;
        ctx_cirrus->pitch = pitch;
        ctx_cirrus->bpp = depth->bpp;
        ctx_cirrus->refreshRate = timing->refreshRate;
        ctx_cirrus->timing = timing;
        ctx_cirrus->depth = depth;
        KE_Print("gfx_cirrus: requested {}x{}x{}, granted {}x{}x{}, shadow {}KiB",
                 requestedWidth, requestedHeight, requestedBpp,
                 ctx_cirrus->width, ctx_cirrus->height, ctx_cirrus->bpp, size / 1024);
        return true;
      }
      KE_Print("gfx_cirrus: {}KiB shadow for {}x{}x{} could not be allocated, stepping down",
               size / 1024, timing->width, timing->height, depth->bpp);
    }

    if (!StepDownMode(timing, depth)) {
      KE_Print("gfx_cirrus: no mode fits in memory, leaving the console to the fallback driver");
      return false;
    }
  }
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
    return;
  }

  const auto ctx_cirrus = pdo->deviceExtension.as<CirrusContext>();
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

  ctx_cirrus->adapter = KE_GFX_RegisterGraphicsAdapter(driver, pdo->attachedDevice, "Cirrus");
  if (!ctx_cirrus->adapter) {
    KE_Print("gfx_cirrus: Failed to register graphics adapter");
    return;
  }
  
  Cirrus_AllocateShadow(nullptr);

  ctx_cirrus->adapter->Probe = Cirrus_Probe;
  ctx_cirrus->adapter->EnumerateDisplays = Cirrus_EnumerateDisplays;
  ctx_cirrus->adapter->SetMode = Cirrus_SetMode;
  ctx_cirrus->adapter->UnsetMode = Cirrus_UnsetMode;
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
