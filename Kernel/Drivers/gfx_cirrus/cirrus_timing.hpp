#pragma once

#include <stdint.h>

namespace cirrus_timing {

struct GtfMode {
  uint32_t width{};
  uint32_t height{};
  uint32_t refreshHz{};
  uint32_t horizontalSyncStart{};
  uint32_t horizontalSyncEnd{};
  uint32_t horizontalTotal{};
  uint32_t verticalSyncStart{};
  uint32_t verticalSyncEnd{};
  uint32_t verticalTotal{};
  uint32_t pixelClockHz{};
};

struct PllCandidate {
  uint8_t numerator{};
  uint8_t denominator{};
  uint8_t denominatorRegister{};
  uint32_t errorPpm{};
};

struct ScanoutLayout {
  uint32_t pitch{};
  uint64_t surfaceBytes{};
  uint64_t memoryClockHz{};
};

constexpr uint32_t kReferenceClockHz = 14318000;
constexpr uint32_t kMaximumPllErrorPpm = 5000;
constexpr uint32_t kGd5434MaxPixelClockSoftwareHz = 80000000;
constexpr uint32_t kGd5434MaxMemoryClockSoftwareHz = 135000000;

enum class Chip : uint8_t { Unknown, Gd5430, Gd5434 };

inline Chip IdentifyChip(uint8_t cr27) {
  switch ((cr27 >> 2) & 0x3f) {
    case 0x28: return Chip::Gd5430;
    case 0x2a: return Chip::Gd5434;
    default: return Chip::Unknown;
  }
}

inline uint32_t MaximumPixelClock(Chip chip, uint32_t bpp) {
  if (chip == Chip::Gd5434) return kGd5434MaxPixelClockSoftwareHz;
  if (chip != Chip::Gd5430) return 0;
  // Xorg's GD5430 table, with a stricter 80-MHz software limit at 8 bpp.
  switch (bpp) {
    case 8: return 80000000;
    case 15:
    case 16: return 50000000;
    case 24: return 28500000;
    default: return 0; // GD5430 has no 32-bpp entry.
  }
}

inline uint32_t MaximumHardwareClock(Chip chip) {
  return chip == Chip::Gd5430 ? 85500000 :
         chip == Chip::Gd5434 ? kGd5434MaxMemoryClockSoftwareHz : 0;
}

inline bool DecodeVram(Chip chip, uint8_t sr0f, uint32_t &outBytes) {
  if (chip == Chip::Unknown) return false;
  // GD5430's SR0F bit 7 is not the GD5434 capacity multiplier.
  uint32_t bytes = 0;
  switch (sr0f & 0x18) {
    case 0x08: bytes = 512 * 1024; break;
    case 0x10: bytes = 1024 * 1024; break;
    case 0x18: bytes = 2 * 1024 * 1024; break;
    default: return false;
  }
  if (chip == Chip::Gd5434 && (sr0f & 0x80)) bytes *= 2;
  outBytes = bytes;
  return true;
}

inline bool DecodeGd5434Vram(uint8_t sr0f, uint32_t &outBytes) {
  return DecodeVram(Chip::Gd5434, sr0f, outBytes);
}

inline bool ComputeScanoutLayout(uint32_t width, uint32_t height, uint32_t bpp,
                                 uint32_t pixelClockHz, uint32_t vramBytes,
                                 ScanoutLayout &out, Chip chip = Chip::Gd5434) {
  uint32_t bytesPerPixel = 0;
  switch (bpp) {
    case 8: bytesPerPixel = 1; break;
    case 15:
    case 16: bytesPerPixel = 2; break;
    case 24: bytesPerPixel = 3; break;
    case 32: bytesPerPixel = 4; break;
    default: return false;
  }
  if (!width || !height || !pixelClockHz ||
      pixelClockHz > MaximumPixelClock(chip, bpp))
    return false;
  const uint64_t rawPitch = static_cast<uint64_t>(width) * bytesPerPixel;
  if (rawPitch > UINT32_MAX - 7) return false;
  const uint64_t pitch = (rawPitch + 7) & ~uint64_t{7};
  const uint64_t surfaceBytes = pitch * height;
  uint32_t clockFactor = bytesPerPixel;
  if ((bpp == 15 || bpp == 16) &&
      (chip == Chip::Gd5430 || vramBytes > 1024 * 1024))
    clockFactor = 1;
  const uint64_t memoryClock =
      static_cast<uint64_t>(pixelClockHz) * clockFactor;
  if (pitch > UINT32_MAX || surfaceBytes > vramBytes ||
      memoryClock > MaximumHardwareClock(chip))
    return false;
  out = ScanoutLayout{
      .pitch = static_cast<uint32_t>(pitch),
      .surfaceBytes = surfaceBytes,
      .memoryClockHz = memoryClock};
  return true;
}

inline bool ComputeGtf(uint32_t width, uint32_t height, uint32_t refreshHz,
                       GtfMode &out) {
  constexpr uint64_t kNsPerSecond = 1000000000;
  constexpr uint64_t kMinimumVSyncBackPorchNs = 550000;
  constexpr uint32_t kHorizontalCell = 8;
  constexpr uint32_t kVerticalSyncLines = 3;
  constexpr uint64_t kCPrimeMilliPercent = 30000;
  constexpr uint64_t kMPrime = 300;

  if (width < 320 || width % kHorizontalCell || height < 200 ||
      height > 1024 || width > 2048 || refreshHz < 50 || refreshHz > 120)
    return false;
  const uint64_t framePeriodNs = kNsPerSecond / refreshHz;
  if (framePeriodNs <= kMinimumVSyncBackPorchNs) return false;
  const uint64_t estimatedLinePeriodNs =
      (framePeriodNs - kMinimumVSyncBackPorchNs) /
      (static_cast<uint64_t>(height) + 1);
  if (!estimatedLinePeriodNs) return false;

  const uint64_t syncBackPorchLines =
      (kMinimumVSyncBackPorchNs + estimatedLinePeriodNs / 2) /
      estimatedLinePeriodNs;
  if (syncBackPorchLines < kVerticalSyncLines ||
      syncBackPorchLines > UINT32_MAX - height - 1)
    return false;
  const uint64_t verticalTotal =
      static_cast<uint64_t>(height) + 1 + syncBackPorchLines;
  const uint64_t lineCountPerSecond =
      static_cast<uint64_t>(refreshHz) * verticalTotal;
  const uint64_t linePeriodNs =
      (kNsPerSecond + lineCountPerSecond / 2) / lineCountPerSecond;
  if (!linePeriodNs) return false;

  const uint64_t dutyReductionMilliPercent =
      (kMPrime * linePeriodNs + 500) / 1000;
  if (dutyReductionMilliPercent >= kCPrimeMilliPercent) return false;
  const uint64_t dutyMilliPercent =
      kCPrimeMilliPercent - dutyReductionMilliPercent;
  const uint64_t blankDenominator = 100000 - dutyMilliPercent;
  const uint64_t blankNumerator =
      static_cast<uint64_t>(width) * dutyMilliPercent;
  const uint64_t unroundedBlank =
      (blankNumerator + blankDenominator / 2) / blankDenominator;
  const uint64_t horizontalBlank =
      ((unroundedBlank + 8) / 16) * 16;
  if (!horizontalBlank || horizontalBlank > UINT32_MAX - width) return false;

  const uint64_t horizontalTotal = width + horizontalBlank;
  const uint64_t horizontalSyncWidth =
      ((horizontalTotal + 50) / 100) * kHorizontalCell;
  const uint64_t horizontalFrontPorch =
      horizontalBlank / 2 > horizontalSyncWidth
          ? horizontalBlank / 2 - horizontalSyncWidth
          : 0;
  if (!horizontalFrontPorch ||
      horizontalFrontPorch > UINT32_MAX - width ||
      horizontalSyncWidth > UINT32_MAX - width - horizontalFrontPorch)
    return false;

  const uint64_t verticalSyncStart = static_cast<uint64_t>(height) + 1;
  const uint64_t verticalSyncEnd =
      verticalSyncStart + kVerticalSyncLines;
  const uint64_t pixelClock =
      (horizontalTotal * kNsPerSecond + linePeriodNs / 2) / linePeriodNs;
  if (pixelClock > UINT32_MAX || horizontalTotal > UINT32_MAX ||
      verticalTotal > UINT32_MAX || verticalSyncEnd > UINT32_MAX)
    return false;

  out = GtfMode{
      .width = width,
      .height = height,
      .refreshHz = refreshHz,
      .horizontalSyncStart =
          static_cast<uint32_t>(width + horizontalFrontPorch),
      .horizontalSyncEnd = static_cast<uint32_t>(
          width + horizontalFrontPorch + horizontalSyncWidth),
      .horizontalTotal = static_cast<uint32_t>(horizontalTotal),
      .verticalSyncStart = static_cast<uint32_t>(verticalSyncStart),
      .verticalSyncEnd = static_cast<uint32_t>(verticalSyncEnd),
      .verticalTotal = static_cast<uint32_t>(verticalTotal),
      .pixelClockHz = static_cast<uint32_t>(pixelClock)};
  return true;
}

inline bool ChoosePll(uint32_t targetClockHz, uint32_t maximumErrorPpm,
                      PllCandidate &out,
                      uint32_t maximumClockHz = kGd5434MaxMemoryClockSoftwareHz,
                      Chip chip = Chip::Gd5434) {
  if (!targetClockHz ||
      targetClockHz > maximumClockHz || !maximumErrorPpm)
    return false;
  bool found = false;
  uint64_t bestError = 0;
  uint64_t bestTarget = 1;
  PllCandidate best{};
  const bool alpine = chip == Chip::Gd5430;
  for (uint32_t numerator = alpine ? 16 : 32;
       numerator <= (alpine ? 126u : 127u); ++numerator) {
    for (uint32_t code = alpine ? 20 : 7; code <= 63; ++code) {
      // Preserve the SD64 effective-divisor search. Alpine instead searches
      // raw five-bit divider/postdivider pairs from Xorg's CirrusFindClock.
      if (!alpine && code > 31 && (code & 1)) continue;
      const uint32_t postdivide = alpine ? code & 1 : (code > 31 ? 1u : 0u);
      const uint32_t registerDivisor = alpine ? code >> 1 : code >> postdivide;
      const uint32_t denominator = registerDivisor * (1 + postdivide);
      const uint64_t outputNumerator =
          static_cast<uint64_t>(kReferenceClockHz) * numerator;
      if (alpine && (outputNumerator < uint64_t{28636000} * registerDivisor ||
                     outputNumerator > uint64_t{111000000} * registerDivisor))
        continue;
      if (outputNumerator >
          static_cast<uint64_t>(maximumClockHz) * denominator)
        continue;
      const uint64_t requestedNumerator =
          static_cast<uint64_t>(targetClockHz) * denominator;
      const uint64_t error = outputNumerator > requestedNumerator
                                 ? outputNumerator - requestedNumerator
                                 : requestedNumerator - outputNumerator;
      if (found && error * bestTarget >= bestError * requestedNumerator)
        continue;
      const uint64_t ppm =
          (error * 1000000 + requestedNumerator / 2) / requestedNumerator;
      if (ppm > maximumErrorPpm) continue;
      found = true;
      bestError = error;
      bestTarget = requestedNumerator;
      best = PllCandidate{
          .numerator = static_cast<uint8_t>(numerator),
          .denominator = static_cast<uint8_t>(denominator),
          .denominatorRegister = static_cast<uint8_t>(
              (alpine ? 0 : 0x80) | (registerDivisor << 1) | postdivide),
          .errorPpm = static_cast<uint32_t>(ppm)};
    }
  }
  if (!found) return false;
  out = best;
  return true;
}

inline bool PackCrtc(const GtfMode &mode, uint32_t pitch, uint8_t (&crtc)[25],
                     uint8_t &extended) {
  if (!mode.width || mode.width > 2048 || !mode.height ||
      mode.height > 1024 || mode.width % 8 ||
      mode.horizontalSyncStart % 8 || mode.horizontalSyncEnd % 8 ||
      mode.horizontalTotal % 8 || pitch % 8 ||
      mode.horizontalTotal < mode.width ||
      mode.horizontalSyncStart < mode.width ||
      mode.horizontalSyncEnd <= mode.horizontalSyncStart ||
      mode.horizontalSyncEnd > mode.horizontalTotal ||
      mode.verticalTotal < mode.height + 4 ||
      mode.verticalSyncStart < mode.height ||
      mode.verticalSyncEnd <= mode.verticalSyncStart ||
      mode.verticalSyncEnd > mode.verticalTotal)
    return false;
  const uint32_t hTotalChars = mode.horizontalTotal / 8;
  const uint32_t hDisplayChars = mode.width / 8;
  const uint32_t hSyncStartChars = mode.horizontalSyncStart / 8;
  const uint32_t hSyncEndChars = mode.horizontalSyncEnd / 8;
  const uint32_t hBlankEndChars = hTotalChars - 1;
  const uint32_t vTotalReg = mode.verticalTotal - 2;
  const uint32_t vDisplayEnd = mode.height - 1;
  const uint32_t vBlankStart = mode.height;
  const uint32_t vBlankEnd = mode.verticalTotal - 1;
  const uint32_t offset = pitch / 8;
  if (hTotalChars < 5 || hTotalChars - 5 > 255 ||
      hDisplayChars - 1 > 255 || hSyncStartChars > 255 ||
      hSyncEndChars > 255 || hBlankEndChars > 255 ||
      mode.verticalTotal < 2 || vTotalReg > 1023 ||
      vDisplayEnd > 1023 || mode.verticalSyncStart > 1023 ||
      mode.verticalSyncEnd - mode.verticalSyncStart > 16 ||
      vBlankStart > 1023 || vBlankEnd > 1023 || offset > 511)
    return false;

  for (auto &value : crtc) value = 0;
  crtc[0x00] = static_cast<uint8_t>(hTotalChars - 5);
  crtc[0x01] = static_cast<uint8_t>(hDisplayChars - 1);
  crtc[0x02] = static_cast<uint8_t>(hDisplayChars);
  crtc[0x03] = static_cast<uint8_t>((hBlankEndChars & 0x1F) | 0x80);
  crtc[0x04] = static_cast<uint8_t>(hSyncStartChars);
  crtc[0x05] = static_cast<uint8_t>((hSyncEndChars & 0x1F) |
                                    ((hBlankEndChars & 0x20) << 2));
  crtc[0x06] = static_cast<uint8_t>(vTotalReg);
  crtc[0x07] = static_cast<uint8_t>(
      ((vTotalReg >> 8) & 0x01) |
      (((vDisplayEnd >> 8) & 0x01) << 1) |
      (((mode.verticalSyncStart >> 8) & 0x01) << 2) |
      (((vBlankStart >> 8) & 0x01) << 3) |
      0x10 |
      (((vTotalReg >> 9) & 0x01) << 5) |
      (((vDisplayEnd >> 9) & 0x01) << 6) |
      (((mode.verticalSyncStart >> 9) & 0x01) << 7));
  crtc[0x09] = static_cast<uint8_t>(
      (((vBlankStart >> 9) & 0x01) << 5) | 0x40);
  crtc[0x10] = static_cast<uint8_t>(mode.verticalSyncStart);
  crtc[0x11] = static_cast<uint8_t>(mode.verticalSyncEnd & 0x0F);
  crtc[0x12] = static_cast<uint8_t>(vDisplayEnd);
  crtc[0x13] = static_cast<uint8_t>(offset);
  crtc[0x14] = 0x40;
  crtc[0x15] = static_cast<uint8_t>(vBlankStart);
  crtc[0x16] = static_cast<uint8_t>(vBlankEnd);
  crtc[0x17] = 0xA3;
  crtc[0x18] = 0xFF;
  extended = static_cast<uint8_t>(0x22 | ((offset & 0x100) ? 0x10 : 0));
  return true;
}

}// namespace cirrus_timing
