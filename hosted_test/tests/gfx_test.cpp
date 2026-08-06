#include <catch2/catch_test_macros.hpp>

#include "ob_test_support.hpp"

#include <kernel/gfx.hpp>
#include <kernel/gfx/KDeviceContextTable.hpp>
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KGraphicsAdapter.hpp>
#include <kernel/kobject/KMemoryBackingObject.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/virtual_memory.hpp>

namespace {

// Fake KGraphicsAdapter backend: cfunc only stores a raw function pointer,
// so recording calls needs module-level state rather than a capturing
// lambda, mirroring how disk_fake provides a fake backend for disk_ata
// tests.
struct FakeAdapterState {
  bool setModeShouldSucceed = true;
  size_t lastSetModeIndex = 0;

  size_t modeCount = 1;
  kernel::KDisplayModeInfo modeInfo{
      .width = 640,
      .height = 480,
      .pitch = 80,
      .bpp = 4,
      .refreshRate = 60,
      .pixelFormat = kernel::PixelFormat::INDEX8,
  };

  bool fillRectCalled = false;
  bool drawLineCalled = false;
  bool bitBltCalled = false;

  int lastX = 0, lastY = 0, lastW = 0, lastH = 0;
  uint32_t lastColor = 0;
};

FakeAdapterState g_fakeAdapterState;

void ResetFakeAdapterState() {
  g_fakeAdapterState = FakeAdapterState{};
}

bool FakeSetMode(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/,
                  kernel::KObjectPtr<kernel::KSurface> surface,
                  size_t modeIndex) {
  g_fakeAdapterState.lastSetModeIndex = modeIndex;
  if (!g_fakeAdapterState.setModeShouldSucceed) {
    return false;
  }
  surface->width = g_fakeAdapterState.modeInfo.width;
  surface->height = g_fakeAdapterState.modeInfo.height;
  surface->pitch = g_fakeAdapterState.modeInfo.pitch;
  surface->pixelFormat = g_fakeAdapterState.modeInfo.pixelFormat;
  return true;
}

size_t FakeGetModeCount() {
  return g_fakeAdapterState.modeCount;
}

bool FakeGetModeInfo(size_t modeIndex, kernel::KDisplayModeInfo &outInfo) {
  if (modeIndex >= g_fakeAdapterState.modeCount) {
    return false;
  }
  outInfo = g_fakeAdapterState.modeInfo;
  return true;
}

bool FakeFillRect(const kernel::KObjectPtr<kernel::KSurface> & /*dst*/,
                   int x, int y, int w, int h, uint32_t colorIndex) {
  g_fakeAdapterState.fillRectCalled = true;
  g_fakeAdapterState.lastX = x;
  g_fakeAdapterState.lastY = y;
  g_fakeAdapterState.lastW = w;
  g_fakeAdapterState.lastH = h;
  g_fakeAdapterState.lastColor = colorIndex;
  return true;
}

bool FakeDrawLine(const kernel::KObjectPtr<kernel::KSurface> & /*dst*/,
                   int x0, int y0, int /*x1*/, int /*y1*/, uint32_t colorIndex) {
  g_fakeAdapterState.drawLineCalled = true;
  g_fakeAdapterState.lastX = x0;
  g_fakeAdapterState.lastY = y0;
  g_fakeAdapterState.lastColor = colorIndex;
  return true;
}

bool FakeBitBlt(const kernel::KObjectPtr<kernel::KSurface> & /*dst*/,
                 int dstX, int dstY,
                 const kernel::KObjectPtr<kernel::KSurface> & /*src*/,
                 int /*srcX*/, int /*srcY*/,
                 uint32_t /*width*/, uint32_t /*height*/,
                 kernel::RasterOp /*rop*/) {
  g_fakeAdapterState.bitBltCalled = true;
  g_fakeAdapterState.lastX = dstX;
  g_fakeAdapterState.lastY = dstY;
  return true;
}

kernel::KObjectPtr<kernel::KDisplay> MakeFakeDisplay() {
  ResetFakeAdapterState();

  auto adapter = KE_GFX_RegisterGraphicsAdapter(nullptr, kernel::KDevicePtr<kernel::KDevice>(), "FakeAdapter");
  REQUIRE(adapter);

  adapter->SetMode = FakeSetMode;
  adapter->FillRect = FakeFillRect;
  adapter->DrawLine = FakeDrawLine;
  adapter->BitBlt = FakeBitBlt;

  auto display = KE_GFX_RegisterDisplay(adapter, "FakeDisplay");
  REQUIRE(display);

  display->GetModeCount = FakeGetModeCount;
  display->GetModeInfo = FakeGetModeInfo;

  return display;
}

}// namespace

TEST_CASE("KE_GFX_CreateSurface populates surface shape from mode info on success", "[gfx]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  const auto surface = KE_GFX_CreateSurfaceForMode(display, 0);

  REQUIRE(surface);
  REQUIRE(surface->width == 640);
  REQUIRE(surface->height == 480);
  REQUIRE(surface->pitch == 80);
  REQUIRE(surface->pixelFormat == kernel::PixelFormat::INDEX8);
  REQUIRE(g_fakeAdapterState.lastSetModeIndex == 0);
}

TEST_CASE("KE_GFX_CreateSurface rejects an out-of-range modeIndex without calling SetMode", "[gfx]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  const auto surface = KE_GFX_CreateSurfaceForMode(display, 1);

  REQUIRE_FALSE(surface);
  // SetMode was never invoked: lastSetModeIndex stays at its reset value.
  REQUIRE(g_fakeAdapterState.lastSetModeIndex == 0);
}

TEST_CASE("KE_GFX_CreateSurface returns null instead of crashing when no driver is bound", "[gfx]") {
  EnsureObjectManagerInitialized();

  auto adapter = KE_GFX_RegisterGraphicsAdapter(nullptr, kernel::KDevicePtr<kernel::KDevice>(), "NoDriverAdapter");
  REQUIRE(adapter);
  // Intentionally leave adapter->SetMode unset (null cfunc).

  auto display = KE_GFX_RegisterDisplay(adapter, "NoDriverDisplay");
  REQUIRE(display);
  display->GetModeCount = FakeGetModeCount;
  display->GetModeInfo = FakeGetModeInfo;

  const auto surface = KE_GFX_CreateSurfaceForMode(display, 0);
  REQUIRE_FALSE(surface);
}

TEST_CASE("KE_GFX_CreateSurface returns null when the driver's SetMode fails", "[gfx]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.setModeShouldSucceed = false;

  const auto surface = KE_GFX_CreateSurfaceForMode(display, 0);
  REQUIRE_FALSE(surface);
}

TEST_CASE("DeviceContextTable Allocate/Resolve/Close round trip", "[gfx][dc_table]") {
  kernel::DeviceContextTable table;

  auto [slot, hdc] = table.Allocate();
  REQUIRE(slot != nullptr);
  REQUIRE(hdc != kernel::INVALID_HDC);

  auto *resolved = table.Resolve(hdc);
  REQUIRE(resolved == slot);

  REQUIRE(table.Close(hdc));
  REQUIRE(table.Resolve(hdc) == nullptr);
}

TEST_CASE("DeviceContextTable rejects a stale HDC after the slot is reused", "[gfx][dc_table]") {
  kernel::DeviceContextTable table;

  auto [slotA, hdcA] = table.Allocate();
  REQUIRE(slotA != nullptr);

  REQUIRE(table.Close(hdcA));

  // Re-allocate: with only one prior slot, this must reuse the same index
  // (via the free list) but bump the generation.
  auto [slotB, hdcB] = table.Allocate();
  REQUIRE(slotB == slotA);
  REQUIRE(hdcB != hdcA);

  // The stale handle captured before the slot was reused must not resolve.
  REQUIRE(table.Resolve(hdcA) == nullptr);
  // The fresh handle must resolve correctly.
  REQUIRE(table.Resolve(hdcB) == slotB);
}

TEST_CASE("DeviceContextTable::Close on an unused/invalid HDC fails", "[gfx][dc_table]") {
  kernel::DeviceContextTable table;

  REQUIRE_FALSE(table.Close(kernel::INVALID_HDC));
  REQUIRE_FALSE(table.Close(0x00010000));// index 0, generation 1: never allocated
}

TEST_CASE("DeviceContextTable grows a new chunk once the first is exhausted", "[gfx][dc_table]") {
  kernel::DeviceContextTable table;

  kstd::vector<kernel::HDC> handles;
  for (size_t i = 0; i < kernel::DeviceContextTable::kSlotsPerChunk; ++i) {
    auto [slot, hdc] = table.Allocate();
    REQUIRE(slot != nullptr);
    handles.push_back(hdc);
  }

  REQUIRE(table.chunks.size() == 1);

  // One more allocation beyond the first chunk's capacity must grow a
  // second chunk rather than fail or alias an existing slot.
  auto [slot, hdc] = table.Allocate();
  REQUIRE(slot != nullptr);
  REQUIRE(table.chunks.size() == 2);

  // All previously allocated handles must still resolve correctly.
  for (auto h: handles) {
    REQUIRE(table.Resolve(h) != nullptr);
  }
  REQUIRE(table.Resolve(hdc) == slot);
}

TEST_CASE("KGraphicsAdapter FillRect/DrawLine/BitBlt forward to the fake driver", "[gfx]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  const auto surface = KE_GFX_CreateSurfaceForMode(display, 0);
  REQUIRE(surface);

  REQUIRE(display->adapter->FillRect(surface, 1, 2, 3, 4, 9));
  REQUIRE(g_fakeAdapterState.fillRectCalled);
  REQUIRE(g_fakeAdapterState.lastX == 1);
  REQUIRE(g_fakeAdapterState.lastY == 2);
  REQUIRE(g_fakeAdapterState.lastW == 3);
  REQUIRE(g_fakeAdapterState.lastH == 4);
  REQUIRE(g_fakeAdapterState.lastColor == 9);

  REQUIRE(display->adapter->DrawLine(surface, 5, 6, 7, 8, 3));
  REQUIRE(g_fakeAdapterState.drawLineCalled);
  REQUIRE(g_fakeAdapterState.lastX == 5);
  REQUIRE(g_fakeAdapterState.lastY == 6);
  REQUIRE(g_fakeAdapterState.lastColor == 3);

  REQUIRE(display->adapter->BitBlt(surface, 10, 11, surface, 0, 0, 4, 4, kernel::RasterOp::SRCCOPY));
  REQUIRE(g_fakeAdapterState.bitBltCalled);
  REQUIRE(g_fakeAdapterState.lastX == 10);
  REQUIRE(g_fakeAdapterState.lastY == 11);
}

TEST_CASE("KE_GFX_SoftwareFillRect/SoftwareDrawLine/SoftwareBitBlt write directly into a bitmap's backing memory", "[gfx][software_fallback]") {
  EnsureObjectManagerInitialized();

  constexpr uint32_t width = 8;
  constexpr uint32_t height = 8;
  const size_t size = static_cast<size_t>(width) * height;// INDEX8: 1 byte/pixel

  auto &as = KE_VMM_GetKernelAddressSpace();
  void *region = KE_VMM_AllocateRegion(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  REQUIRE(region != nullptr);

  auto backing = KE_VMM_CreateDirectBacking(kernel::VirtualAddress::from_ptr(region), size);
  REQUIRE(backing);

  auto surface = kernel::CreateKObject<kernel::KSurface>("SoftwareFallbackSurface");
  REQUIRE(surface);
  surface->backing = backing;
  surface->width = width;
  surface->height = height;
  surface->pitch = width;
  surface->pixelFormat = kernel::PixelFormat::INDEX8;

  REQUIRE(KE_GFX_SoftwareFillRect(surface, 0, 0, 4, 4, 0x42));
  auto *pixels = surface->backing->directAddress.as_ptr<uint8_t>();
  for (uint32_t y = 0; y < 4; ++y) {
    for (uint32_t x = 0; x < 4; ++x) {
      REQUIRE(pixels[y * width + x] == 0x42);
    }
  }

  REQUIRE(KE_GFX_SoftwareDrawLine(surface, 0, 0, 7, 0, 0x11));
  REQUIRE(pixels[0] == 0x11);
  REQUIRE(pixels[7] == 0x11);

  auto dst = kernel::CreateKObject<kernel::KSurface>("SoftwareFallbackDst");
  REQUIRE(dst);
  void *dstRegion = KE_VMM_AllocateRegion(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  REQUIRE(dstRegion != nullptr);
  dst->backing = KE_VMM_CreateDirectBacking(kernel::VirtualAddress::from_ptr(dstRegion), size);
  REQUIRE(dst->backing);
  dst->width = width;
  dst->height = height;
  dst->pitch = width;
  dst->pixelFormat = kernel::PixelFormat::INDEX8;

  REQUIRE(KE_GFX_SoftwareBitBlt(dst, 0, 0, surface, 0, 0, 4, 4, kernel::RasterOp::SRCCOPY));
  auto *dstPixels = dst->backing->directAddress.as_ptr<uint8_t>();
  // Row 0 was overwritten by the DrawLine above; check a pixel from the
  // untouched part of the filled rect instead (row 1, col 1).
  REQUIRE(dstPixels[1 * width + 1] == 0x42);

  // Only SRCCOPY is implemented in the software fallback.
  REQUIRE_FALSE(KE_GFX_SoftwareBitBlt(dst, 0, 0, surface, 0, 0, 4, 4, kernel::RasterOp::SRCAND));
}

TEST_CASE("DeviceContextTable slot fields round-trip through the SpinLock-guarded slot", "[gfx][dc_table]") {
  EnsureObjectManagerInitialized();

  kernel::DeviceContextTable table;
  const auto display = MakeFakeDisplay();

  auto [slot, hdc] = table.Allocate();
  REQUIRE(slot != nullptr);

  slot->lock.lock();
  slot->display = display;
  slot->target = KE_GFX_CreateSurfaceForMode(display, 0);
  slot->penColor = 7;
  slot->currentX = 10;
  slot->currentY = 20;
  slot->lock.unlock();

  auto *resolved = table.Resolve(hdc);
  REQUIRE(resolved == slot);
  REQUIRE(resolved->display == display);
  REQUIRE(resolved->target);
  REQUIRE(resolved->penColor == 7);
  REQUIRE(resolved->currentX == 10);
  REQUIRE(resolved->currentY == 20);
}
