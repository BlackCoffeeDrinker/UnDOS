#include "gfx_register_mock.hpp"
#define HAL_IO_Out8 gfx_register_mock::Out8
#define HAL_IO_In8 gfx_register_mock::In8
#define KE_RES_FindAssigned gfx_register_mock::FindAssigned
#define KE_RES_ReleaseResources gfx_register_mock::ReleaseResources
#define DriverEntry HostedCirrusDriverEntry
#include "../../../Kernel/Drivers/gfx_cirrus/cirrus.cpp"
#undef DriverEntry
#undef KE_RES_ReleaseResources
#undef KE_RES_FindAssigned
#undef HAL_IO_In8
#undef HAL_IO_Out8
#include "ob_test_support.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {
uint8_t window[65536]{};
kernel::KObjectPtr<kernel::KDisplay> MakeCirrus() {
  EnsureObjectManagerInitialized();
  static uint32_t counter{};
  char name[32];
  snprintf(name, sizeof(name), "hosted-cirrus-%u", ++counter);
  auto device = KE_IO_CreateDeviceWithContext<CirrusContext>(
      nullptr, name, kernel::device_type::Video);
  REQUIRE(device);
  auto adapter = kernel::CreateAnonymousKObject<kernel::KGraphicsAdapter>().value();
  adapter->device = *device;
  auto *ctx = adapter->GetDeviceExtensionAs<CirrusContext>();
  ctx->chip = cirrus_timing::Chip::Gd5434;
  ctx->vramBytes = 2 * 1024 * 1024;
  ctx->window = kernel::VirtualAddress::from_ptr(window);
  auto target = kernel::CreateAnonymousKObject<kernel::KDisplay>().value();
  target->adapter = adapter;
  target->isPresent = true;
  target->monitorCaps = kernel::KMonitorCapabilities::BuiltinFallback();
  REQUIRE_FALSE(Cirrus_ConfigureDisplay(adapter, target));
  return target;
}
}// namespace

TEST_CASE("Cirrus real callbacks preserve blanking and encode the requested hardware clock",
          "[gfx][cirrus][backend]") {
  const auto target = MakeCirrus();
  auto *ctx = target->adapter->GetDeviceExtensionAs<CirrusContext>();
  for (const uint32_t vram: {1024u * 1024, 2u * 1024 * 1024}) {
    ctx->vramBytes = vram;
    for (const auto &depth: cirrus_regs::kDepths) {
      CirrusModeCandidate candidate{};
      const bool built = Cirrus_BuildMode(*target, *ctx, 640, 480, 60, depth, candidate);
      if (uint64_t{640} * 480 * ((depth.bpp + 7) / 8) > vram) {
        REQUIRE_FALSE(built);
        continue;
      }
      REQUIRE(built);
      ctx->activeTiming = candidate.timing;
      ctx->activeDepth = candidate.depth;
      ctx->timing = &ctx->activeTiming;
      ctx->depth = &ctx->activeDepth;
      ctx->pitch = candidate.info.pitch;
      REQUIRE_FALSE(target->Disable(target));
      gfx_register_mock::requireBlank = true;
      gfx_register_mock::unblanked = false;
      REQUIRE_FALSE(Cirrus_ProgramMode(ctx));
      gfx_register_mock::requireBlank = false;
      REQUIRE_FALSE(gfx_register_mock::unblanked);
      REQUIRE_FALSE(gfx_register_mock::attributeVideo);
      const uint32_t sr1e = gfx_register_mock::sequencer[0x1e];
      const uint32_t divisor = ((sr1e >> 1) & 63) * (1 + (sr1e & 1));
      REQUIRE(divisor != 0);
      uint32_t factor = depth.bpp == 24 ? 3 : (depth.bpp == 32 ? 4 : 1);
      if ((depth.bpp == 15 || depth.bpp == 16) && vram <= 1024 * 1024) factor = 2;
      const uint64_t desired = uint64_t{candidate.timing.pixelClockHz} * factor * divisor;
      const uint64_t programmed = uint64_t{14318000} * gfx_register_mock::sequencer[0x0e];
      const auto error = desired > programmed ? desired - programmed : programmed - desired;
      REQUIRE(error * 1000000 <= desired * 5000);
      REQUIRE_FALSE(target->Enable(target));
      REQUIRE(gfx_register_mock::attributeVideo);
      REQUIRE((gfx_register_mock::sequencer[1] & 0x20) == 0);
    }
  }
}

TEST_CASE("Cirrus reservations prepare without I/O and restore an old backing",
          "[gfx][cirrus][backend]") {
  const auto target = MakeCirrus();
  auto *ctx = target->adapter->GetDeviceExtensionAs<CirrusContext>();
  ctx->chip = GENERATE(cirrus_timing::Chip::Gd5434, cirrus_timing::Chip::Gd5430);
  REQUIRE_FALSE(target->Validate(target));
  kernel::KDisplayModeReservation first{};
  const auto writes = gfx_register_mock::writes;
  REQUIRE_FALSE(target->PrepareMode(target, 0, first));
  REQUIRE(gfx_register_mock::writes == writes);
  REQUIRE_FALSE(target->Disable(target));
  REQUIRE_FALSE(target->ProgramMode(target, first));
  REQUIRE_FALSE(target->Enable(target));
  target->FinishMode(target, first, true);
  const auto previous = ctx->shadow;
  kernel::KDisplayModeReservation next{};
  REQUIRE_FALSE(target->PrepareMode(target, 1, next));
  REQUIRE_FALSE(target->Disable(target));
  SECTION("A programmed candidate can be restored") {
    REQUIRE_FALSE(target->ProgramMode(target, next));
  }
  SECTION("Locked extension registers fail programming and restoration truthfully") {
    gfx_register_mock::extensionsLocked = true;
    const auto programmed = target->ProgramMode(target, next);
    const auto restored = target->RestoreMode(target, 0, next);
    gfx_register_mock::extensionsLocked = false;
    REQUIRE(programmed == kernel::KernelError::DeviceError);
    REQUIRE(restored == kernel::KernelError::DeviceError);
    REQUIRE_FALSE(target->QueryActualState(target)->known);
  }
  REQUIRE_FALSE(target->RestoreMode(target, 0, next));
  REQUIRE_FALSE(target->QueryActualState(target)->enabled);
  REQUIRE_FALSE(target->Enable(target));
  REQUIRE(target->QueryActualState(target)->modeIndex == 0);
  REQUIRE(ctx->shadow == previous);
  target->FinishMode(target, next, false);
  KE_Free(ctx->shadow.as_ptr<void>());
  ctx->shadow = nullptr;
}

TEST_CASE("Cirrus programmed retrace endpoints decode to the requested absolute lines",
          "[gfx][cirrus][backend]") {
  const auto target = MakeCirrus();
  target->monitorCaps = {};
  auto *ctx = target->adapter->GetDeviceExtensionAs<CirrusContext>();
  const struct {
    uint32_t width, height;
  } geometries[] = {
      {640, 480},
      {800, 600},
      {1024, 768}};
  for (const auto geometry: geometries) {
    CirrusModeCandidate candidate{};
    REQUIRE(Cirrus_BuildMode(*target, *ctx, geometry.width, geometry.height, 60,
                             cirrus_regs::kDepths[4], candidate));
    ctx->activeTiming = candidate.timing;
    ctx->activeDepth = candidate.depth;
    ctx->timing = &ctx->activeTiming;
    ctx->depth = &ctx->activeDepth;
    ctx->pitch = candidate.info.pitch;
    REQUIRE_FALSE(target->Disable(target));
    REQUIRE_FALSE(Cirrus_ProgramMode(ctx));
    const auto *crtc = gfx_register_mock::crtc;
    const uint32_t start = crtc[0x10] | ((crtc[7] & 4u) << 6) |
                           ((crtc[7] & 0x80u) << 2);
    uint32_t end = (start & ~15u) | (crtc[0x11] & 15u);
    if (end <= start) end += 16;
    REQUIRE(start == geometry.height + 1);
    REQUIRE(end == geometry.height + 4);
  }
}

TEST_CASE("Cirrus detection separates GD5430 capacity from GD5434 bank doubling",
          "[gfx][cirrus][backend][gd5430]") {
  const auto target = MakeCirrus();
  auto *ctx = target->adapter->GetDeviceExtensionAs<CirrusContext>();
  for (const auto revision: {0u, 1u, 2u, 3u}) {
    gfx_register_mock::crtc[0x27] = static_cast<uint8_t>(0xa0 | revision);
    for (const auto memory: {0x08u, 0x10u, 0x18u, 0x88u, 0x90u, 0x98u}) {
      gfx_register_mock::sequencer[0x0f] = static_cast<uint8_t>(memory);
      REQUIRE_FALSE(Cirrus_DetectHardware(*ctx));
      REQUIRE(ctx->chip == cirrus_timing::Chip::Gd5430);
      REQUIRE(ctx->chipId == 0x28);
      const uint32_t expected = (memory & 0x18) == 8 ? 512 * 1024 : (memory & 0x18) == 16 ? 1024 * 1024
                                                                                          : 2 * 1024 * 1024;
      REQUIRE(ctx->vramBytes == expected);
    }
  }
  gfx_register_mock::crtc[0x27] = 0xa8;
  gfx_register_mock::sequencer[0x0f] = 0x98;
  REQUIRE_FALSE(Cirrus_DetectHardware(*ctx));
  REQUIRE(ctx->chip == cirrus_timing::Chip::Gd5434);
  REQUIRE(ctx->vramBytes == 4 * 1024 * 1024);
  gfx_register_mock::crtc[0x27] = 0xa0;
  gfx_register_mock::sequencer[0x0f] = 0x80;
  REQUIRE(Cirrus_DetectHardware(*ctx) == kernel::KernelError::Unsupported);
  REQUIRE(ctx->vramBytes == 0);
  gfx_register_mock::crtc[0x27] = 0xb8;// GD5446 is not GD5430.
  REQUIRE(Cirrus_DetectHardware(*ctx) == kernel::KernelError::Unsupported);
  REQUIRE(ctx->chip == cirrus_timing::Chip::Unknown);
}

TEST_CASE("GD5430 callbacks program chip-specific depth and decoded hardware clock",
          "[gfx][cirrus][backend][gd5430]") {
  const auto target = MakeCirrus();
  auto *ctx = target->adapter->GetDeviceExtensionAs<CirrusContext>();
  gfx_register_mock::crtc[0x27] = 0xa0;
  gfx_register_mock::sequencer[0x0f] = 0x18;// QEMU's non-GD5446 reset.
  REQUIRE_FALSE(Cirrus_DetectHardware(*ctx));
  REQUIRE_FALSE(target->Validate(target));
  REQUIRE_FALSE(Cirrus_Probe(target->adapter));
  size_t modes = 0;
  for (; modes < 64; ++modes) {
    const auto info = target->GetModeInfo(target, modes);
    if (!info) break;
    REQUIRE(info->bpp != 32);
    kernel::KDisplayModeReservation reservation{};
    const auto writes = gfx_register_mock::writes;
    REQUIRE_FALSE(target->PrepareMode(target, modes, reservation));
    REQUIRE(gfx_register_mock::writes == writes);
    REQUIRE_FALSE(target->Disable(target));
    gfx_register_mock::requireBlank = true;
    gfx_register_mock::unblanked = false;
    const auto error = target->ProgramMode(target, reservation);
    gfx_register_mock::requireBlank = false;
    REQUIRE_FALSE(error);
    REQUIRE_FALSE(gfx_register_mock::unblanked);
    const uint32_t raw = gfx_register_mock::sequencer[0x1e];
    const uint32_t divisor = ((raw >> 1) & 31) * (1 + (raw & 1));
    REQUIRE(divisor > 0);
    REQUIRE((raw & 0xc0) == 0);
    const uint32_t n = gfx_register_mock::sequencer[0x0e];
    REQUIRE(n >= 16);
    REQUIRE(n <= 126);
    const uint32_t d = (raw >> 1) & 31;
    REQUIRE(d >= 10);
    REQUIRE(uint64_t{14318000} * n >= uint64_t{28636000} * d);
    REQUIRE(uint64_t{14318000} * n <= uint64_t{111000000} * d);
    const uint64_t clock = uint64_t{14318000} * gfx_register_mock::sequencer[0x0e];
    const uint32_t factor = info->bpp == 24 ? 3 : 1;
    const uint64_t requested = uint64_t{ctx->timing->pixelClockHz} * factor * divisor;
    const auto delta = clock > requested ? clock - requested : requested - clock;
    REQUIRE(delta * 1000000 <= requested * 5000);
    REQUIRE(clock <= uint64_t{85500000} * divisor);
    const uint8_t expectedSr07 = info->bpp == 8 ? 1 : info->bpp == 24 ? 5
                                                                      : 7;
    const uint8_t expectedDac = info->bpp == 8 ? 0 : info->bpp == 15 ? 0xc0
                                                 : info->bpp == 16   ? 0xc1
                                                                     : 0xc5;
    REQUIRE(gfx_register_mock::sequencer[7] == expectedSr07);
    REQUIRE(gfx_register_mock::hiddenDac == expectedDac);
    REQUIRE_FALSE(target->QueryActualState(target)->enabled);
    REQUIRE_FALSE(target->Enable(target));
    REQUIRE(target->QueryActualState(target)->enabled);
    target->FinishMode(target, reservation, true);
  }
  REQUIRE(modes == 4);
  KE_Free(ctx->shadow.as_ptr<void>());
  ctx->shadow = nullptr;
}

TEST_CASE("GD5430 rejects unsupported depths clocks and insufficient VRAM",
          "[gfx][cirrus][backend][gd5430]") {
  const auto target = MakeCirrus();
  target->monitorCaps = {};
  auto *ctx = target->adapter->GetDeviceExtensionAs<CirrusContext>();
  ctx->chip = cirrus_timing::Chip::Gd5430;
  CirrusModeCandidate candidate{};
  REQUIRE_FALSE(Cirrus_BuildMode(*target, *ctx, 640, 480, 60, cirrus_regs::kDepths[0], candidate));
  REQUIRE_FALSE(Cirrus_BuildMode(*target, *ctx, 800, 600, 60, cirrus_regs::kDepths[1], candidate));
  REQUIRE_FALSE(Cirrus_BuildMode(*target, *ctx, 1024, 768, 60, cirrus_regs::kDepths[2], candidate));
  REQUIRE(Cirrus_BuildMode(*target, *ctx, 800, 600, 60, cirrus_regs::kDepths[2], candidate));
  ctx->vramBytes = 512 * 1024;
  REQUIRE_FALSE(Cirrus_BuildMode(*target, *ctx, 640, 480, 60, cirrus_regs::kDepths[2], candidate));
  REQUIRE(Cirrus_BuildMode(*target, *ctx, 640, 480, 60, cirrus_regs::kDepths[4], candidate));
  ctx->vramBytes = 1024 * 1024;
  REQUIRE(Cirrus_BuildMode(*target, *ctx, 640, 480, 60, cirrus_regs::kDepths[2], candidate));
  const uint32_t raw = candidate.timing.vclkDenominator;
  const uint32_t divisor = ((raw >> 1) & 63) * (1 + (raw & 1));
  const uint64_t requested = uint64_t{candidate.timing.pixelClockHz} * divisor;
  const uint64_t clock = uint64_t{14318000} * candidate.timing.vclkNumerator;
  const auto delta = clock > requested ? clock - requested : requested - clock;
  REQUIRE(delta * 1000000 <= requested * 5000);// No GD5434 1-MiB doubling.
}
