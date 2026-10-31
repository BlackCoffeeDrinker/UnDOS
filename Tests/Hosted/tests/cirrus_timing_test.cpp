#include <catch2/catch_test_macros.hpp>

#include "../../../Kernel/Drivers/gfx_cirrus/cirrus_timing.hpp"

TEST_CASE("Cirrus GTF calculations match reference modelines", "[gfx][cirrus][timing]") {
  struct Vector {
    uint32_t width;
    uint32_t height;
    uint32_t horizontalSyncStart;
    uint32_t horizontalSyncEnd;
    uint32_t horizontalTotal;
    uint32_t verticalSyncStart;
    uint32_t verticalSyncEnd;
    uint32_t verticalTotal;
    uint32_t pixelClockHz;
  };
  constexpr Vector vectors[] = {
      {640, 480, 656, 720, 800, 481, 484, 497, 23860000},
      {800, 600, 832, 912, 1024, 601, 604, 622, 38220000},
      {1024, 768, 1080, 1184, 1344, 769, 772, 795, 64110000},
  };
  for (const auto &expected : vectors) {
    cirrus_timing::GtfMode mode{};
    REQUIRE(cirrus_timing::ComputeGtf(expected.width, expected.height, 60, mode));
    REQUIRE(mode.horizontalSyncStart == expected.horizontalSyncStart);
    REQUIRE(mode.horizontalSyncEnd == expected.horizontalSyncEnd);
    REQUIRE(mode.horizontalTotal == expected.horizontalTotal);
    REQUIRE(mode.verticalSyncStart == expected.verticalSyncStart);
    REQUIRE(mode.verticalSyncEnd == expected.verticalSyncEnd);
    REQUIRE(mode.verticalTotal == expected.verticalTotal);
    REQUIRE(((mode.pixelClockHz + 5000) / 10000) * 10000 ==
            expected.pixelClockHz);
  }

  const struct {
    uint32_t width, height;
  } lineCompareGeometries[] = {{800, 600}, {1024, 768}};
  cirrus_timing::GtfMode mode{};
  uint8_t crtc[25]{};
  uint8_t extended{};
  for (const auto &geometry : lineCompareGeometries) {
    REQUIRE(cirrus_timing::ComputeGtf(geometry.width, geometry.height, 60, mode));
    REQUIRE(cirrus_timing::PackCrtc(mode, 2048, crtc, extended));
    const uint32_t lineCompare =
        crtc[0x18] | ((static_cast<uint32_t>(crtc[0x07]) & 0x10) << 4) |
        ((static_cast<uint32_t>(crtc[0x09]) & 0x40) << 3);
    REQUIRE(lineCompare == 0x3FF);
    const uint32_t expectedOverflow =
        (((mode.verticalTotal - 2) >> 8) & 0x01) |
        ((((mode.height - 1) >> 8) & 0x01) << 1) |
        (((mode.verticalSyncStart >> 8) & 0x01) << 2) |
        (((mode.height >> 8) & 0x01) << 3) |
        (((mode.verticalTotal - 2) >> 9) & 0x01) << 5 |
        (((mode.height - 1) >> 9) & 0x01) << 6 |
        (((mode.verticalSyncStart >> 9) & 0x01) << 7);
    REQUIRE((crtc[0x07] & ~0x10u) == expectedOverflow);
    REQUIRE((crtc[0x09] & ~0x40u) ==
            (((mode.height >> 9) & 0x01) << 5));
  }
}

TEST_CASE("Cirrus GTF and PLL reject invalid inputs and choose bounded candidates", "[gfx][cirrus][timing]") {
  cirrus_timing::GtfMode mode{};
  REQUIRE_FALSE(cirrus_timing::ComputeGtf(639, 480, 60, mode));
  REQUIRE_FALSE(cirrus_timing::ComputeGtf(640, 480, 0, mode));
  REQUIRE_FALSE(cirrus_timing::ComputeGtf(640, 480, 121, mode));

  cirrus_timing::PllCandidate pll{};
  REQUIRE(cirrus_timing::ChoosePll(23860000, cirrus_timing::kMaximumPllErrorPpm, pll));
  REQUIRE(pll.numerator >= 32);
  REQUIRE(pll.numerator <= 127);
  REQUIRE(pll.denominator >= 7);
  REQUIRE(pll.denominator <= 63);
  REQUIRE(pll.errorPpm <= cirrus_timing::kMaximumPllErrorPpm);
  const uint64_t pllNumerator =
      static_cast<uint64_t>(cirrus_timing::kReferenceClockHz) *
      pll.numerator;
  const uint32_t decodedDivisor =
      ((pll.denominatorRegister >> 1) & 0x3f) *
      (1u + (pll.denominatorRegister & 1u));
  REQUIRE(decodedDivisor == pll.denominator);
  const uint64_t requestedNumerator =
      static_cast<uint64_t>(23860000) * decodedDivisor;
  const uint64_t pllError = pllNumerator > requestedNumerator
                                ? pllNumerator - requestedNumerator
                                : requestedNumerator - pllNumerator;
  REQUIRE((pllError * 1000000 + requestedNumerator / 2) /
              requestedNumerator <=
          cirrus_timing::kMaximumPllErrorPpm);
  REQUIRE_FALSE(cirrus_timing::ChoosePll(0, 5000, pll));
  REQUIRE_FALSE(cirrus_timing::ChoosePll(135000001, 5000, pll));
}

TEST_CASE("GD5434 VRAM decoding accepts only approved SR0F encodings", "[gfx][cirrus][timing]") {
  uint32_t bytes = 0;
  REQUIRE(cirrus_timing::DecodeGd5434Vram(0x08, bytes));
  REQUIRE(bytes == 512 * 1024);
  REQUIRE(cirrus_timing::DecodeGd5434Vram(0x10, bytes));
  REQUIRE(bytes == 1024 * 1024);
  REQUIRE(cirrus_timing::DecodeGd5434Vram(0x18, bytes));
  REQUIRE(bytes == 2 * 1024 * 1024);
  REQUIRE(cirrus_timing::DecodeGd5434Vram(0x98, bytes));
  REQUIRE(bytes == 4 * 1024 * 1024);
  REQUIRE_FALSE(cirrus_timing::DecodeGd5434Vram(0x00, bytes));
  REQUIRE_FALSE(cirrus_timing::DecodeGd5434Vram(0x20, bytes));
}

TEST_CASE("GD5434 scanout layout preserves 1024x768x16 within 2 MiB", "[gfx][cirrus][timing]") {
  cirrus_timing::GtfMode mode{};
  REQUIRE(cirrus_timing::ComputeGtf(1024, 768, 60, mode));
  cirrus_timing::ScanoutLayout layout{};
  REQUIRE(cirrus_timing::ComputeScanoutLayout(
      1024, 768, 16, mode.pixelClockHz, 2 * 1024 * 1024, layout));
  REQUIRE(layout.pitch == 2048);
  REQUIRE(layout.surfaceBytes == 1572864);
  REQUIRE(layout.memoryClockHz == mode.pixelClockHz);
  REQUIRE(layout.memoryClockHz <=
          cirrus_timing::kGd5434MaxMemoryClockSoftwareHz);
  REQUIRE_FALSE(cirrus_timing::ComputeScanoutLayout(
      1024, 768, 16, mode.pixelClockHz, 1024 * 1024, layout));
  REQUIRE_FALSE(cirrus_timing::ComputeScanoutLayout(
      1024, 768, 24, mode.pixelClockHz, 2 * 1024 * 1024, layout));
  REQUIRE_FALSE(cirrus_timing::ComputeScanoutLayout(
      1024, 768, 12, mode.pixelClockHz, 2 * 1024 * 1024, layout));
  cirrus_timing::GtfMode lowMode{};
  REQUIRE(cirrus_timing::ComputeGtf(640, 480, 60, lowMode));
  REQUIRE(cirrus_timing::ComputeScanoutLayout(
      640, 480, 16, lowMode.pixelClockHz, 1024 * 1024, layout));
  REQUIRE(layout.memoryClockHz ==
          static_cast<uint64_t>(lowMode.pixelClockHz) * 2);
}

TEST_CASE("GD5434 scanout layout applies depth and memory clock factors", "[gfx][cirrus][timing]") {
  cirrus_timing::ScanoutLayout layout{};
  constexpr uint32_t kClockHz = 1000000;
  constexpr uint32_t kVramBytes = 4 * 1024 * 1024;
  const struct {
    uint32_t bpp;
    uint64_t factor;
  } cases[] = {
      {8, 1},
      {15, 1},
      {16, 1},
      {24, 3},
      {32, 4},
  };
  for (const auto &testCase : cases) {
    REQUIRE(cirrus_timing::ComputeScanoutLayout(
        320, 200, testCase.bpp, kClockHz, kVramBytes, layout));
    REQUIRE(layout.memoryClockHz == kClockHz * testCase.factor);
  }
  REQUIRE(cirrus_timing::ComputeScanoutLayout(
      320, 200, 15, kClockHz, 1024 * 1024, layout));
  REQUIRE(layout.memoryClockHz == kClockHz * 2);
}

TEST_CASE("Cirrus CRTC packing preserves timing fields and rejects truncation", "[gfx][cirrus][timing]") {
  cirrus_timing::GtfMode mode{};
  REQUIRE(cirrus_timing::ComputeGtf(1024, 768, 60, mode));
  uint8_t crtc[25]{};
  uint8_t extended{};
  REQUIRE(cirrus_timing::PackCrtc(mode, 2048, crtc, extended));
  REQUIRE((static_cast<uint32_t>(crtc[0]) + 5) * 8 == mode.horizontalTotal);
  REQUIRE((static_cast<uint32_t>(crtc[1]) + 1) * 8 == mode.width);
  REQUIRE((static_cast<uint32_t>(crtc[4]) * 8) == mode.horizontalSyncStart);
  const uint32_t horizontalSyncStartChars = crtc[4];
  const uint32_t horizontalSyncEndChars =
      (horizontalSyncStartChars & ~0x1Fu) | (crtc[5] & 0x1F);
  const uint32_t horizontalSyncWidthChars =
      (horizontalSyncEndChars - horizontalSyncStartChars) & 0x1F;
  REQUIRE((horizontalSyncStartChars + horizontalSyncWidthChars) * 8 ==
          mode.horizontalSyncEnd);
  const uint32_t verticalTotal =
      static_cast<uint32_t>(crtc[6]) |
      ((static_cast<uint32_t>(crtc[7]) & 0x01) << 8) |
      ((static_cast<uint32_t>(crtc[7]) & 0x20) << 4);
  REQUIRE(verticalTotal + 2 == mode.verticalTotal);
  const uint32_t verticalDisplayEnd =
      static_cast<uint32_t>(crtc[0x12]) |
      ((static_cast<uint32_t>(crtc[7]) & 0x02) << 7) |
      ((static_cast<uint32_t>(crtc[7]) & 0x40) << 3);
  REQUIRE(verticalDisplayEnd + 1 == mode.height);
  const uint32_t verticalSyncStart =
      static_cast<uint32_t>(crtc[0x10]) |
      ((static_cast<uint32_t>(crtc[7]) & 0x04) << 6) |
      ((static_cast<uint32_t>(crtc[7]) & 0x80) << 2);
  REQUIRE(verticalSyncStart == mode.verticalSyncStart);
  const uint32_t verticalBlankStart =
      static_cast<uint32_t>(crtc[0x15]) |
      ((static_cast<uint32_t>(crtc[7]) & 0x08) << 5) |
      ((static_cast<uint32_t>(crtc[0x09]) & 0x20) << 4);
  REQUIRE(verticalBlankStart == mode.height);
  const uint32_t decodedEnd = verticalSyncStart +
      (((static_cast<uint32_t>(crtc[0x11] & 0x0F) - verticalSyncStart - 1) & 15) + 1);
  REQUIRE(decodedEnd == mode.verticalSyncEnd);
  REQUIRE((static_cast<uint32_t>(crtc[0x13]) |
           ((extended & 0x10) ? 0x100u : 0u)) == 256);
  REQUIRE((extended & 0x10) == 0x10);
  REQUIRE_FALSE(cirrus_timing::PackCrtc(mode, 4096, crtc, extended));
}
