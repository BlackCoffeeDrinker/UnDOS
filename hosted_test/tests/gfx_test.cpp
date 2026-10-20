#include <catch2/catch_test_macros.hpp>

#include "ob_test_support.hpp"

#include "gfx/gfx.hpp"

#include <kernel/gfx.hpp>
#include <kernel/gfx/KDeviceContextTable.hpp>
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KGraphicsAdapter.hpp>
#include <kernel/kobject/KMemoryBackingObject.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/object_manager.hpp>
#include <kernel/virtual_memory.hpp>
#include "../../userland/apps/init/display_config.hpp"
#include "syscalls/syscalls.hpp"
#include "user/user.hpp"

namespace {

// Fake KGraphicsAdapter backend: cfunc only stores a raw function pointer,
// so recording calls needs module-level state rather than a capturing
// lambda, mirroring how disk_fake provides a fake backend for disk_ata
// tests.
struct FakeAdapterState {
  // --- ProbeDisplay ---
  size_t headCount = 1;      // ProbeDisplay reports false at this index
  bool firstHeadAbsent = false;// head 0 reports isPresent = false

  // --- ConfigureDisplay ---
  bool configureShouldSucceed = true;
  bool bindGetModeInfo = true;
  bool bindSetMode = true;
  size_t configureCalls = 0;
  uint32_t lastConfiguredHead = 0xFFFFFFFFu;

  // --- SetMode ---
  bool setModeShouldSucceed = true;
  bool enableShouldSucceed = true;
  size_t setModeCalls = 0;
  size_t lastSetModeIndex = 0;

  // Mode list the fake display reports. Mode 0 is the only one the default
  // monitor capabilities accept; mode 1 exercises the runtime switch.
  size_t modeCount = 1;
  kernel::KDisplayModeInfo modes[2]{
      {
          .width = 640,
          .height = 480,
          .pitch = 80,
          .bpp = 4,
          .refreshRate = 60,
          .pixelFormat = kernel::PixelFormat::INDEX8,
      },
      {
          .width = 800,
          .height = 600,
          .pitch = 800,
          .bpp = 8,
          .refreshRate = 60,
          .pixelFormat = kernel::PixelFormat::INDEX8,
      },
  };

  // Hook ordering log: 'D' = Disable, 'S' = SetMode, 'E' = Enable.
  char callLog[64]{};
  size_t callLogLen = 0;

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

// Clears the recorded calls while keeping the configured behaviour, so a test
// is not confused by whatever the kernel did while starting the adapter.
void ResetFakeCalls() {
  g_fakeAdapterState.configureCalls = 0;
  g_fakeAdapterState.setModeCalls = 0;
  g_fakeAdapterState.lastSetModeIndex = 0;
  g_fakeAdapterState.callLogLen = 0;
  for (auto &c: g_fakeAdapterState.callLog) c = '\0';
}

void RecordCall(const char tag) {
  if (g_fakeAdapterState.callLogLen + 1 < sizeof(g_fakeAdapterState.callLog)) {
    g_fakeAdapterState.callLog[g_fakeAdapterState.callLogLen++] = tag;
  }
}

kstd::string_view CallLog() {
  return {g_fakeAdapterState.callLog, g_fakeAdapterState.callLogLen};
}

kstd::expected<kernel::KDisplayModeInfo, kstd::error_code> FakeGetModeInfo(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/, size_t modeIndex) {
  if (modeIndex >= g_fakeAdapterState.modeCount) {
    return kstd::unexpected(kernel::KernelError::OutOfRange);
  }
  kernel::KDisplayModeInfo info = g_fakeAdapterState.modes[modeIndex];
  info.modeIndex = modeIndex;
  return info;
}

kstd::expected<kernel::KDisplaySurfaceBinding, kstd::error_code> FakeSetMode(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/,
                                                                              size_t modeIndex) {
  RecordCall('S');
  g_fakeAdapterState.setModeCalls++;
  g_fakeAdapterState.lastSetModeIndex = modeIndex;
  if (!g_fakeAdapterState.setModeShouldSucceed) {
    return kstd::unexpected(kernel::KernelError::DeviceError);
  }
  kernel::KDisplaySurfaceBinding binding{};
  binding.type = kernel::SurfaceType::Framebuffer;
  binding.flags = kernel::SurfaceFlags::TopDown;
  return binding;
}

kstd::error_code FakeEnable(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/) {
  RecordCall('E');
  return g_fakeAdapterState.enableShouldSucceed ? kernel::KernelError::Success : kernel::KernelError::DeviceError;
}

void FakeDisable(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/) {
  RecordCall('D');
}

kstd::error_code FakeProbeDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                                     const kernel::KGraphicsAdapterDisplayEnumInfo &enumInfo,
                                     kernel::KGraphicsAdapterDisplayInfo &outInfo) {
  if (enumInfo.displayIndex >= g_fakeAdapterState.headCount) {
    return kernel::KernelError::OutOfRange;
  }

  outInfo.isPresent = !(g_fakeAdapterState.firstHeadAbsent && enumInfo.displayIndex == 0);
  outInfo.isMonitor = outInfo.isPresent;
  outInfo.isPnP = false;
  outInfo.edidSize = 0;
  outInfo.name = "FakeHead";
  return kernel::KernelError::Success;
}

kstd::error_code FakeConfigureDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                                         const kernel::KObjectPtr<kernel::KDisplay> &display) {
  g_fakeAdapterState.configureCalls++;
  g_fakeAdapterState.lastConfiguredHead = display->headIndex;

  if (!g_fakeAdapterState.configureShouldSucceed) {
    return kernel::KernelError::DeviceError;
  }

  if (g_fakeAdapterState.bindGetModeInfo) {
    display->GetModeInfo = FakeGetModeInfo;
  }
  if (g_fakeAdapterState.bindSetMode) {
    display->SetMode = FakeSetMode;
  }
  display->Enable = FakeEnable;
  display->Disable = FakeDisable;
  return kernel::KernelError::Success;
}

kstd::error_code FakeFillRect(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                  const kernel::KObjectPtr<kernel::KSurface> & /*dst*/,
                  int x, int y, int w, int h, uint32_t colorIndex) {
  g_fakeAdapterState.fillRectCalled = true;
  g_fakeAdapterState.lastX = x;
  g_fakeAdapterState.lastY = y;
  g_fakeAdapterState.lastW = w;
  g_fakeAdapterState.lastH = h;
  g_fakeAdapterState.lastColor = colorIndex;
  return kernel::KernelError::Success;
}

kstd::error_code FakeDrawLine(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                  const kernel::KObjectPtr<kernel::KSurface> & /*dst*/,
                  int x0, int y0, int /*x1*/, int /*y1*/, uint32_t colorIndex) {
  g_fakeAdapterState.drawLineCalled = true;
  g_fakeAdapterState.lastX = x0;
  g_fakeAdapterState.lastY = y0;
  g_fakeAdapterState.lastColor = colorIndex;
  return kernel::KernelError::Success;
}

kstd::error_code FakeBitBlt(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                const kernel::KObjectPtr<kernel::KSurface> & /*dst*/,
                int dstX, int dstY,
                const kernel::KObjectPtr<kernel::KSurface> & /*src*/,
                int /*srcX*/, int /*srcY*/,
                uint32_t /*width*/, uint32_t /*height*/,
                kernel::RasterOp /*rop*/) {
  g_fakeAdapterState.bitBltCalled = true;
  g_fakeAdapterState.lastX = dstX;
  g_fakeAdapterState.lastY = dstY;
  return kernel::KernelError::Success;
}

kernel::KObjectPtr<kernel::KGraphicsAdapter> MakeFakeAdapter() {
  static uint32_t s_fakeCounter = 0;
  char adapterName[32];
  snprintf(adapterName, sizeof(adapterName), "FakeAdapter%u", ++s_fakeCounter);

  auto registered = KE_GFX_RegisterGraphicsAdapter(nullptr, kernel::KDevicePtr<kernel::KDevice>(), adapterName);
  REQUIRE(registered.has_value());
  auto adapter = *registered;

  adapter->ProbeDisplay = FakeProbeDisplay;
  adapter->ConfigureDisplay = FakeConfigureDisplay;
  adapter->FillRect = FakeFillRect;
  adapter->DrawLine = FakeDrawLine;
  adapter->BitBlt = FakeBitBlt;

  return adapter;
}

// The GFX subsystem is the sole creator of KDisplay objects, so the created
// heads are resolved out of the object namespace rather than returned.
kstd::vector<kernel::KObjectPtr<kernel::KDisplay>> DisplaysOf(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) {
  kstd::vector<kernel::KObjectPtr<kernel::KDisplay>> displays;
  KE_OB_GetGfxDirectory()->Each<kernel::KDisplay>([&](const kernel::KObjectPtr<kernel::KDisplay> &display) {
    if (display->adapter == adapter) {
      displays.push_back(display);
    }
  });
  return displays;
}

// Registers an adapter and drives it through the kernel's probe loop with
// whatever behaviour the fake state currently describes.
kstd::vector<kernel::KObjectPtr<kernel::KDisplay>> StartFakeAdapter() {
  const auto adapter = MakeFakeAdapter();
  kernel::gfx::start_adapter(adapter);
  return DisplaysOf(adapter);
}

kernel::KObjectPtr<kernel::KDisplay> MakeFakeDisplay() {
  ResetFakeAdapterState();

  auto displays = StartFakeAdapter();
  REQUIRE(displays.size() == 1);

  // A non-PnP head is constrained to the default monitor capabilities; the
  // mode tests drive the whole fake list, so lift that constraint here. The
  // capability filter itself is covered by the [gfx][monitor] cases.
  displays[0]->monitorCaps = kernel::KMonitorCapabilities{};

  ResetFakeCalls();
  return displays[0];
}

}// namespace

TEST_CASE("KE_GFX_CreateSurface populates surface shape from mode info on success", "[gfx]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  const auto surface = KE_GFX_CreateSurfaceForMode(display, 0).value_or(nullptr);

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
  REQUIRE(surface.error() == kernel::KernelError::OutOfRange);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
}

TEST_CASE("KE_GFX_CreateSurface returns null instead of crashing when no driver is bound", "[gfx]") {
  EnsureObjectManagerInitialized();

  ResetFakeAdapterState();
  // ConfigureDisplay deliberately leaves display->SetMode unset (null cfunc).
  g_fakeAdapterState.bindSetMode = false;

  auto displays = StartFakeAdapter();
  REQUIRE(displays.size() == 1);
  REQUIRE_FALSE(displays[0]->SetMode);

  const auto surface = KE_GFX_CreateSurfaceForMode(displays[0], 0);
  REQUIRE_FALSE(surface);
  REQUIRE(surface.error() == kernel::KernelError::Unsupported);
}

TEST_CASE("KE_GFX_CreateSurface returns null when the driver's SetMode fails", "[gfx]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.setModeShouldSucceed = false;

  const auto surface = KE_GFX_CreateSurfaceForMode(display, 0);
  REQUIRE_FALSE(surface);
  REQUIRE(surface.error() == kernel::KernelError::DeviceError);
}

TEST_CASE("DeviceContextTable Allocate/Resolve/Close round trip", "[gfx][dc_table]") {
  kernel::DeviceContextTable table;

  auto slotAlloc = table.Allocate();
  REQUIRE(slotAlloc.has_value());
  auto [slot, hdc] = *slotAlloc;
  REQUIRE(slot != nullptr);
  REQUIRE(hdc != kernel::INVALID_HDC);

  auto *resolved = table.Resolve(hdc);
  REQUIRE(resolved == slot);

  REQUIRE(table.Close(hdc) == kernel::KernelError::Success);
  REQUIRE(table.Resolve(hdc) == nullptr);
}

TEST_CASE("DeviceContextTable rejects a stale HDC after the slot is reused", "[gfx][dc_table]") {
  kernel::DeviceContextTable table;

  auto slotAAlloc = table.Allocate();
  REQUIRE(slotAAlloc.has_value());
  auto [slotA, hdcA] = *slotAAlloc;
  REQUIRE(slotA != nullptr);
  slotA->presentationDirty = true;
  slotA->presentationBounds = {.left = 1, .top = 2, .right = 3, .bottom = 4};

  REQUIRE(table.Close(hdcA) == kernel::KernelError::Success);

  // Re-allocate: with only one prior slot, this must reuse the same index
  // (via the free list) but bump the generation.
  auto slotBAlloc = table.Allocate();
  REQUIRE(slotBAlloc.has_value());
  auto [slotB, hdcB] = *slotBAlloc;
  REQUIRE(slotB == slotA);
  REQUIRE(hdcB != hdcA);
  REQUIRE_FALSE(slotB->presentationDirty);
  REQUIRE(slotB->presentationBounds.left == 0);
  REQUIRE(slotB->presentationBounds.top == 0);
  REQUIRE(slotB->presentationBounds.right == 0);
  REQUIRE(slotB->presentationBounds.bottom == 0);

  // The stale handle captured before the slot was reused must not resolve.
  REQUIRE(table.Resolve(hdcA) == nullptr);
  // The fresh handle must resolve correctly.
  REQUIRE(table.Resolve(hdcB) == slotB);
}

TEST_CASE("DeviceContextTable::Close on an unused/invalid HDC fails", "[gfx][dc_table]") {
  kernel::DeviceContextTable table;

  REQUIRE(table.Close(kernel::INVALID_HDC) == kernel::KernelError::InvalidHandle);
  REQUIRE(table.Close(0x00010000) == kernel::KernelError::InvalidHandle);// index 0, generation 1: never allocated
}

TEST_CASE("DeviceContextTable grows a new chunk once the first is exhausted", "[gfx][dc_table]") {
  kernel::DeviceContextTable table;

  kstd::vector<kernel::HDC> handles;
  for (size_t i = 0; i < kernel::DeviceContextTable::kSlotsPerChunk; ++i) {
    auto slotAlloc = table.Allocate();
  REQUIRE(slotAlloc.has_value());
  auto [slot, hdc] = *slotAlloc;
    REQUIRE(slot != nullptr);
    handles.push_back(hdc);
  }

  REQUIRE(table.chunks.size() == 1);

  // One more allocation beyond the first chunk's capacity must grow a
  // second chunk rather than fail or alias an existing slot.
  auto slotAlloc = table.Allocate();
  REQUIRE(slotAlloc.has_value());
  auto [slot, hdc] = *slotAlloc;
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
  const auto surface = KE_GFX_CreateSurfaceForMode(display, 0).value_or(nullptr);
  REQUIRE(surface);

  REQUIRE(display->adapter->FillRect(display->adapter, surface, 1, 2, 3, 4, 9) == kernel::KernelError::Success);
  REQUIRE(g_fakeAdapterState.fillRectCalled);
  REQUIRE(g_fakeAdapterState.lastX == 1);
  REQUIRE(g_fakeAdapterState.lastY == 2);
  REQUIRE(g_fakeAdapterState.lastW == 3);
  REQUIRE(g_fakeAdapterState.lastH == 4);
  REQUIRE(g_fakeAdapterState.lastColor == 9);

  REQUIRE(display->adapter->DrawLine(display->adapter, surface, 5, 6, 7, 8, 3) == kernel::KernelError::Success);
  REQUIRE(g_fakeAdapterState.drawLineCalled);
  REQUIRE(g_fakeAdapterState.lastX == 5);
  REQUIRE(g_fakeAdapterState.lastY == 6);
  REQUIRE(g_fakeAdapterState.lastColor == 3);

  REQUIRE(display->adapter->BitBlt(display->adapter, surface, 10, 11, surface, 0, 0, 4, 4, kernel::RasterOp::SRCCOPY) == kernel::KernelError::Success);
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
  void *region = KE_VMM_AllocateRegion(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE).value();
  REQUIRE(region != nullptr);

  auto backing = KE_VMM_CreateDirectBacking(kernel::VirtualAddress::from_ptr(region), size).value();
  REQUIRE(backing);

  auto surface = kernel::CreateKObject<kernel::KSurface>("SoftwareFallbackSurface").value();
  REQUIRE(surface);
  surface->backing = backing;
  surface->width = width;
  surface->height = height;
  surface->pitch = width;
  surface->pixelFormat = kernel::PixelFormat::INDEX8;

  REQUIRE(KE_GFX_SoftwareFillRect(surface, 0, 0, 4, 4, 0x42) == kernel::KernelError::Success);
  auto *pixels = surface->backing->directAddress.as_ptr<uint8_t>();
  for (uint32_t y = 0; y < 4; ++y) {
    for (uint32_t x = 0; x < 4; ++x) {
      REQUIRE(pixels[y * width + x] == 0x42);
    }
  }

  REQUIRE(KE_GFX_SoftwareDrawLine(surface, 0, 0, 7, 0, 0x11) == kernel::KernelError::Success);
  REQUIRE(pixels[0] == 0x11);
  REQUIRE(pixels[7] == 0x11);

  auto dst = kernel::CreateKObject<kernel::KSurface>("SoftwareFallbackDst").value();
  REQUIRE(dst);
  void *dstRegion = KE_VMM_AllocateRegion(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE).value();
  REQUIRE(dstRegion != nullptr);
  dst->backing = KE_VMM_CreateDirectBacking(kernel::VirtualAddress::from_ptr(dstRegion), size).value();
  REQUIRE(dst->backing);
  dst->width = width;
  dst->height = height;
  dst->pitch = width;
  dst->pixelFormat = kernel::PixelFormat::INDEX8;

  REQUIRE(KE_GFX_SoftwareBitBlt(dst, 0, 0, surface, 0, 0, 4, 4, kernel::RasterOp::SRCCOPY) == kernel::KernelError::Success);
  auto *dstPixels = dst->backing->directAddress.as_ptr<uint8_t>();
  // Row 0 was overwritten by the DrawLine above; check a pixel from the
  // untouched part of the filled rect instead (row 1, col 1).
  REQUIRE(dstPixels[1 * width + 1] == 0x42);

  // Only SRCCOPY is implemented in the software fallback.
  REQUIRE(KE_GFX_SoftwareBitBlt(dst, 0, 0, surface, 0, 0, 4, 4, kernel::RasterOp::SRCAND) == kernel::KernelError::Unsupported);
}

TEST_CASE("DeviceContextTable slot fields round-trip through the SpinLock-guarded slot", "[gfx][dc_table]") {
  EnsureObjectManagerInitialized();

  kernel::DeviceContextTable table;
  const auto display = MakeFakeDisplay();

  auto slotAlloc = table.Allocate();
  REQUIRE(slotAlloc.has_value());
  auto [slot, hdc] = *slotAlloc;
  REQUIRE(slot != nullptr);

  slot->lock.lock();
  slot->display = display;
  slot->target = KE_GFX_CreateSurfaceForMode(display, 0).value_or(nullptr);
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

TEST_CASE("Packed raster bounds and self-overlap preserve source pixels", "[gfx][software_fallback]") {
  EnsureObjectManagerInitialized();
  auto &as = KE_VMM_GetKernelAddressSpace();
  void *region = KE_VMM_AllocateRegion(as, 64, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE).value();
  REQUIRE(region);
  auto surface = kernel::CreateKObject<kernel::KSurface>("ClippedCopyBitmap").value();
  REQUIRE(surface);
  surface->backing = KE_VMM_CreateDirectBacking(kernel::VirtualAddress::from_ptr(region), 64).value();
  surface->width = 8;
  surface->height = 8;
  surface->pitch = 8;
  surface->pixelFormat = kernel::PixelFormat::INDEX8;
  auto *pixels = static_cast<uint8_t *>(region);
  for (uint32_t i = 0; i < 64; ++i) pixels[i] = static_cast<uint8_t>(i);

  REQUIRE(KE_GFX_SoftwareBitBlt(surface, 1, 1, surface, 0, 0, 7, 7, kernel::RasterOp::SRCCOPY) == kernel::KernelError::Success);
  for (uint32_t y = 1; y < 8; ++y)
    for (uint32_t x = 1; x < 8; ++x) REQUIRE(pixels[y * 8 + x] == (y - 1) * 8 + x - 1);

  for (uint32_t i = 0; i < 64; ++i) pixels[i] = static_cast<uint8_t>(i);
  REQUIRE(KE_GFX_SoftwareBitBlt(surface, -2, 0, surface, 1, 0, 8, 8, kernel::RasterOp::SRCCOPY) == kernel::KernelError::Success);
  for (uint32_t y = 0; y < 8; ++y)
    for (uint32_t x = 0; x < 5; ++x) REQUIRE(pixels[y * 8 + x] == y * 8 + x + 3);

  REQUIRE(KE_GFX_SoftwareFillRect(surface, -20, -20, 22, 22, 0xee) == kernel::KernelError::Success);
  REQUIRE(pixels[0] == 0xee);
  REQUIRE(pixels[9] == 0xee);
  REQUIRE(pixels[2] != 0xee);
  REQUIRE(pixels[16] != 0xee);
  REQUIRE(KE_GFX_SoftwareDrawLine(surface, INT32_MIN, 7, INT32_MAX, 7, 0xcc) == kernel::KernelError::Success);
  for (uint32_t x = 0; x < 8; ++x) REQUIRE(pixels[56 + x] == 0xcc);

  surface->type = kernel::SurfaceType::Framebuffer;
  surface->flags = kernel::SurfaceFlags::TopDown;
  REQUIRE(KE_GFX_SoftwareFillRect(surface, 0, 0, 8, 8, 0xff) != kernel::KernelError::Success);
  REQUIRE(KE_GFX_SoftwareBitBlt(surface, 0, 0, surface, 0, 0, 8, 8, kernel::RasterOp::SRCCOPY) != kernel::KernelError::Success);
  REQUIRE(pixels[0] == 0xee);

  surface->type = kernel::SurfaceType::OffscreenBitmap;
  surface->backing->size = 63;
  REQUIRE(KE_GFX_SoftwareFillRect(surface, 0, 0, 1, 1, 0xff) != kernel::KernelError::Success);
  surface->backing->size = 64;
  surface->pitch = -8;
  surface->backing->directAddress = kernel::VirtualAddress::from_ptr(pixels + 56);
  REQUIRE(KE_GFX_SoftwareFillRect(surface, 0, 0, 2, 2, 0xab) == kernel::KernelError::Success);
  REQUIRE(pixels[56] == 0xab);
  REQUIRE(pixels[49] == 0xab);
  REQUIRE(pixels[48 + 2] != 0xab);
}

TEST_CASE("Software fallbacks treat RGB15/RGB16 as two bytes per pixel", "[gfx][software_fallback]") {
  EnsureObjectManagerInitialized();

  constexpr uint32_t width = 8;
  constexpr uint32_t height = 4;
  constexpr uint32_t pitch = width * 2;
  const size_t size = static_cast<size_t>(pitch) * height;

  auto &as = KE_VMM_GetKernelAddressSpace();

  auto make = [&](kernel::PixelFormat format, const char *name) {
    void *region = KE_VMM_AllocateRegion(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE).value();
    REQUIRE(region != nullptr);
    auto surface = kernel::CreateKObject<kernel::KSurface>(name).value();
    REQUIRE(surface);
    surface->backing = KE_VMM_CreateDirectBacking(kernel::VirtualAddress::from_ptr(region), size).value();
    REQUIRE(surface->backing);
    surface->width = width;
    surface->height = height;
    surface->pitch = pitch;
    surface->pixelFormat = format;
    return surface;
  };

  const auto src = make(kernel::PixelFormat::RGB16, "Rgb16Src");
  REQUIRE(KE_GFX_SoftwareFillRect(src, 0, 0, 4, 2, 0xF81F) == kernel::KernelError::Success);

  auto *srcPixels = src->backing->directAddress.as_ptr<uint16_t>();
  REQUIRE(srcPixels[0] == 0xF81F);
  REQUIRE(srcPixels[3] == 0xF81F);
  // The fifth pixel of row 0 is outside the filled rect, proving the two-byte stride.
  REQUIRE(srcPixels[4] == 0);
  REQUIRE(srcPixels[pitch / 2] == 0xF81F);

  // Same-format blit takes the memcpy fast path.
  const auto dst = make(kernel::PixelFormat::RGB16, "Rgb16Dst");
  REQUIRE(KE_GFX_SoftwareBitBlt(dst, 1, 1, src, 0, 0, 4, 2, kernel::RasterOp::SRCCOPY) == kernel::KernelError::Success);
  auto *dstPixels = dst->backing->directAddress.as_ptr<uint16_t>();
  REQUIRE(dstPixels[pitch / 2 + 1] == 0xF81F);
  REQUIRE(dstPixels[pitch / 2 + 0] == 0);

  // Direct RGB magenta quantizes to the closest canonical VGA palette color.
  const auto index = make(kernel::PixelFormat::INDEX8, "Index8Dst");
  index->pitch = width;
  REQUIRE(KE_GFX_SoftwareBitBlt(index, 0, 0, src, 0, 0, 4, 1, kernel::RasterOp::SRCCOPY) == kernel::KernelError::Success);
  auto *indexPixels = index->backing->directAddress.as_ptr<uint8_t>();
  REQUIRE(indexPixels[0] == 13);
  REQUIRE(KE_GFX_SoftwareFillRect(index, 0, 0, 2, 1, 1) == kernel::KernelError::Success);
  REQUIRE(KE_GFX_SoftwareBitBlt(dst, 0, 0, index, 0, 0, 2, 1, kernel::RasterOp::SRCCOPY) == kernel::KernelError::Success);
  REQUIRE(dstPixels[0] == 0x0015);// Palette blue = 0x0000aa, not raw integer 1.
  index->type = kernel::SurfaceType::Framebuffer;
  index->flags = kernel::SurfaceFlags::Linear;
  REQUIRE(KE_GFX_SoftwareFillRect(index, 0, 0, 2, 1, 2) == kernel::KernelError::Success);// Cirrus 3:3:2 blue.
  REQUIRE(KE_GFX_SoftwareBitBlt(dst, 0, 0, index, 0, 0, 2, 1, kernel::RasterOp::SRCCOPY) == kernel::KernelError::Success);
  REQUIRE(dstPixels[0] == 0x0015);

  // RGB15 shares the two-byte layout.
  const auto rgb15 = make(kernel::PixelFormat::RGB15, "Rgb15Dst");
  REQUIRE(KE_GFX_SoftwareFillRect(rgb15, 2, 0, 1, 1, 0x7C1F) == kernel::KernelError::Success);
  REQUIRE(rgb15->backing->directAddress.as_ptr<uint16_t>()[2] == 0x7C1F);
}

// ---------------------------------------------------------------------------
// Probe loop / ConfigureDisplay
// ---------------------------------------------------------------------------

TEST_CASE("StartAdapter creates one display per present head with its head index", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  ResetFakeAdapterState();
  g_fakeAdapterState.headCount = 2;

  auto displays = StartFakeAdapter();
  REQUIRE(displays.size() == 2);
  REQUIRE(g_fakeAdapterState.configureCalls == 2);

  bool sawHead0 = false, sawHead1 = false;
  for (const auto &display: displays) {
    if (display->headIndex == 0) sawHead0 = true;
    if (display->headIndex == 1) sawHead1 = true;
    // The hooks ConfigureDisplay bound are visible on the created object.
    REQUIRE(display->GetModeInfo);
    REQUIRE(display->SetMode);
    REQUIRE(display->isPresent);
  }
  REQUIRE(sawHead0);
  REQUIRE(sawHead1);

  // Some display has been elected primary by the subsystem.
  REQUIRE(KE_GFX_GetPrimaryDisplay());
}

TEST_CASE("StartAdapter discards a head whose ConfigureDisplay fails", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  ResetFakeAdapterState();
  g_fakeAdapterState.configureShouldSucceed = false;

  const auto adapter = MakeFakeAdapter();
  kernel::gfx::start_adapter(adapter);

  REQUIRE(g_fakeAdapterState.configureCalls == 1);
  REQUIRE(DisplaysOf(adapter).empty());
}

TEST_CASE("StartAdapter skips a head reporting isPresent = false", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  ResetFakeAdapterState();
  g_fakeAdapterState.headCount = 2;
  g_fakeAdapterState.firstHeadAbsent = true;

  auto displays = StartFakeAdapter();
  REQUIRE(displays.size() == 1);
  REQUIRE(displays[0]->headIndex == 1);
  // ConfigureDisplay is only called for heads that produced a KDisplay.
  REQUIRE(g_fakeAdapterState.configureCalls == 1);
  REQUIRE(g_fakeAdapterState.lastConfiguredHead == 1);
}

// ---------------------------------------------------------------------------
// Runtime mode changes
// ---------------------------------------------------------------------------

TEST_CASE("Adapter discovery and mode enumeration never activate graphics", "[gfx][mode]") {
  EnsureObjectManagerInitialized();
  ResetFakeAdapterState();
  g_fakeAdapterState.modeCount = 2;
  const auto displays = StartFakeAdapter();
  REQUIRE(displays.size() == 1);
  const auto &display = displays[0];
  REQUIRE_FALSE(display->surface);
  REQUIRE_FALSE(display->isAttachedToDesktop);
  REQUIRE(display->currentMode.width == 0);
  REQUIRE(display->modeGeneration == 0);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
  REQUIRE(CallLog().empty());
  display->monitorCaps = {};
  REQUIRE(display->monitorCaps.Add({800, 600, 60}) == kernel::KernelError::Success);
  const auto mode = kernel::gfx::enumerate_mode(display, 0);
  REQUIRE(mode.has_value());
  REQUIRE(mode->modeIndex == 1);
  REQUIRE(mode->width == 800);
  REQUIRE(kernel::gfx::enumerate_mode(display, 1).error() == kernel::KernelError::NotFound);
  REQUIRE(kernel::gfx::enumerate_mode(display, UINT32_MAX).error() == kernel::KernelError::NotFound);
  REQUIRE(kernel::gfx::enumerate_mode(nullptr, 0).error() == kernel::KernelError::InvalidArgument);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
}

TEST_CASE("Mode enumeration preserves driver failures instead of reporting exhaustion", "[gfx][mode][kernel-error]") {
  EnsureObjectManagerInitialized();
  auto display = kernel::CreateKObject<kernel::KDisplay>("EnumerationErrorDisplay").value();
  display->GetModeInfo = [](const kernel::KObjectPtr<kernel::KDisplay> &, size_t)
      -> kstd::expected<kernel::KDisplayModeInfo, kstd::error_code> {
    return kstd::unexpected(kernel::KernelError::DeviceNotReady);
  };
  REQUIRE(kernel::gfx::enumerate_mode(display, 0).error() == kernel::KernelError::DeviceNotReady);
  display->GetModeInfo = [](const kernel::KObjectPtr<kernel::KDisplay> &, size_t)
      -> kstd::expected<kernel::KDisplayModeInfo, kstd::error_code> {
    return kstd::unexpected(kernel::KernelError::OutOfRange);
  };
  REQUIRE(kernel::gfx::enumerate_mode(display, 0).error() == kernel::KernelError::NotFound);
}

TEST_CASE("Display syscalls query inactive state and keep root surfaces across switches", "[gfx][mode]") {
  EnsureObjectManagerInitialized();
  const auto created = MakeFakeDisplay();
  const auto display = KE_GFX_GetPrimaryDisplay();
  REQUIRE(display);
  struct Restore {
    kernel::KObjectPtr<kernel::KDisplay> display;
    decltype(display->GetModeInfo) getMode;
    decltype(display->SetMode) setMode;
    decltype(display->Enable) enable;
    decltype(display->Disable) disable;
    kernel::KObjectPtr<kernel::KSurface> surface;
    kernel::KDisplayModeInfo mode;
    kernel::KMonitorCapabilities caps;
    uint32_t generation;
    bool attached;
    ~Restore() {
      kernel::user::Guard guard;
      kernel::user::cursor_hide_locked();
      kernel::user::cursor_reset_locked();
      display->GetModeInfo = getMode;
      display->SetMode = setMode;
      display->Enable = enable;
      display->Disable = disable;
      display->surface = surface;
      display->currentMode = mode;
      display->monitorCaps = caps;
      display->modeGeneration = generation;
      display->isAttachedToDesktop = attached;
    }
  } restore{.display = display, .getMode = display->GetModeInfo, .setMode = display->SetMode, .enable = display->Enable, .disable = display->Disable,
            .surface = display->surface, .mode = display->currentMode, .caps = display->monitorCaps,
            .generation = display->modeGeneration, .attached = display->isAttachedToDesktop};
  display->GetModeInfo = created->GetModeInfo;
  display->SetMode = created->SetMode;
  display->Enable = created->Enable;
  display->Disable = created->Disable;
  display->surface = nullptr;
  display->currentMode = {};
  display->monitorCaps = {};
  display->modeGeneration = 0;
  display->isAttachedToDesktop = false;
  g_fakeAdapterState.modeCount = 2;

  __KRN_GFX_GETDISPLAYMODE_V1 query{};
  REQUIRE(handle_GetDisplayMode_V1(kstd::move(query)) == kernel::KernelError::Success);
  REQUIRE(query.state.active == 0);
  REQUIRE(query.state.generation == 0);
  REQUIRE(query.state.mode.width == 0);
  REQUIRE(kernel::user::screen_rect().Empty());
  REQUIRE_FALSE(display->surface);

  __KRN_GFX_ENUMDISPLAYMODES_V1 enumeration{};
  enumeration.index = 1;
  REQUIRE(handle_EnumDisplayModes_V1(kstd::move(enumeration)) == kernel::KernelError::Success);
  REQUIRE(enumeration.available == 1);
  REQUIRE(enumeration.mode.modeId == 1);
  REQUIRE(enumeration.mode.width == 800);
  enumeration.index = UINT32_MAX;
  REQUIRE(handle_EnumDisplayModes_V1(kstd::move(enumeration)) == kernel::KernelError::Success);
  REQUIRE(enumeration.available == 0);
  REQUIRE(enumeration.mode.width == 0);
  REQUIRE_FALSE(display->surface);

  __KRN_GFX_SETDISPLAYMODE_V1 set{0};
  REQUIRE(handle_SetDisplayMode_V1(kstd::move(set)) == kernel::KernelError::Success);
  const auto surface = display->surface;
  REQUIRE(surface);
  REQUIRE(display->isAttachedToDesktop);
  set.modeId = UINT32_MAX;
  REQUIRE(handle_SetDisplayMode_V1(kstd::move(set)) != kernel::KernelError::Success);
  REQUIRE(display->modeGeneration == 1);
  set.modeId = 1;
  REQUIRE(handle_SetDisplayMode_V1(kstd::move(set)) == kernel::KernelError::Success);
  REQUIRE(display->surface == surface);
  REQUIRE(surface->width == 800);
  REQUIRE(handle_GetDisplayMode_V1(kstd::move(query)) == kernel::KernelError::Success);
  REQUIRE(query.state.active == 1);
  REQUIRE(query.state.generation == 2);
  REQUIRE(query.state.mode.modeId == 1);
  REQUIRE(handle_SetDisplayMode_V1(kstd::move(set)) == kernel::KernelError::Success);
  REQUIRE(display->modeGeneration == 2);
}

TEST_CASE("Output enable failure is explicit and leaves valid programmed state", "[gfx][mode]") {
  EnsureObjectManagerInitialized();
  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.enableShouldSucceed = false;
  REQUIRE(KE_GFX_SetDisplayMode(display, 0) == kernel::KernelError::DeviceError);
  REQUIRE(display->surface);
  REQUIRE(display->currentMode.width == 640);
  REQUIRE_FALSE(display->isAttachedToDesktop);
  REQUIRE(display->modeGeneration == 1);
  g_fakeAdapterState.enableShouldSucceed = true;
  REQUIRE(KE_GFX_SetDisplayMode(display, 0) == kernel::KernelError::Success);
  REQUIRE(display->isAttachedToDesktop);
  REQUIRE(display->modeGeneration == 2);
}

TEST_CASE("Initial programming failure does not invent an active surface", "[gfx][mode]") {
  EnsureObjectManagerInitialized();
  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.setModeShouldSucceed = false;
  REQUIRE(KE_GFX_SetDisplayMode(display, 0) == kernel::KernelError::DeviceError);
  REQUIRE_FALSE(display->surface);
  REQUIRE_FALSE(display->isAttachedToDesktop);
  REQUIRE(display->modeGeneration == 0);
  REQUIRE(CallLog() == "DS");
}

TEST_CASE("Display configuration is bounded and rejects approximate input", "[gfx][mode]") {
  init::DisplayPreference pref{};
  const auto parse = [&](const char *text) {
    return init::parse_display_config(text, __builtin_strlen(text), pref);
  };
  REQUIRE(parse("640 480 8 60\n"));
  REQUIRE(pref.width == 640);
  REQUIRE(pref.bpp == 8);
  REQUIRE(parse(" 800\t600 16 0\r\n"));
  REQUIRE(pref.refreshHz == 0);
  REQUIRE_FALSE(parse(""));
  REQUIRE_FALSE(parse("640 480 8"));
  REQUIRE_FALSE(parse("640 480 8 60 extra"));
  REQUIRE_FALSE(parse("640x480 8 60"));
  REQUIRE_FALSE(parse("0 480 8 60"));
  REQUIRE_FALSE(parse("32768 480 8 60"));
  REQUIRE_FALSE(parse("640 480 33 60"));
  REQUIRE_FALSE(parse("4294967296 480 8 60"));
  const char embedded[] = "640 480 8 60\0junk";
  REQUIRE_FALSE(init::parse_display_config(embedded, sizeof(embedded) - 1, pref));
}

TEST_CASE("KE_GFX_SetDisplayMode reuses the same KSurface across a mode switch", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.modeCount = 2;

  const auto surface = KE_GFX_CreateSurfaceForMode(display, 0).value_or(nullptr);
  REQUIRE(surface);
  REQUIRE(surface->width == 640);
  REQUIRE(display->currentMode.width == 640);

  REQUIRE(KE_GFX_SetDisplayMode(display, 1) == kernel::KernelError::Success);

  // Same object: HDCs already holding the surface stay valid.
  REQUIRE(display->surface == surface);
  REQUIRE(surface->width == 800);
  REQUIRE(surface->height == 600);
  REQUIRE(surface->pitch == 800);
  REQUIRE(surface->bpp == 8);
  REQUIRE(surface->sizeInBytes == 800u * 600u);
  REQUIRE(display->currentMode.width == 800);
  REQUIRE(display->currentMode.modeIndex == 1);
}

TEST_CASE("KE_GFX_SetDisplayMode disables before programming and enables after", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.modeCount = 2;
  ResetFakeCalls();

  REQUIRE(KE_GFX_SetDisplayMode(display, 1) == kernel::KernelError::Success);
  REQUIRE(CallLog() == "DSE");
}

TEST_CASE("KE_GFX_SetDisplayMode rejects an out-of-range index without programming", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();

  REQUIRE(KE_GFX_SetDisplayMode(display, 7) == kernel::KernelError::OutOfRange);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
}

TEST_CASE("A failing driver SetMode leaves the previous surface and re-enables the output", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.modeCount = 2;

  const auto surface = KE_GFX_CreateSurfaceForMode(display, 0).value_or(nullptr);
  REQUIRE(surface);

  g_fakeAdapterState.setModeShouldSucceed = false;
  ResetFakeCalls();

  REQUIRE(KE_GFX_SetDisplayMode(display, 1) != kernel::KernelError::Success);

  // Previous mode stays programmed, and the output is brought back up.
  REQUIRE(display->surface == surface);
  REQUIRE(surface->width == 640);
  REQUIRE(display->currentMode.width == 640);
  REQUIRE(CallLog() == "DSE");
}

// ---------------------------------------------------------------------------
// Monitor capabilities
// ---------------------------------------------------------------------------

TEST_CASE("KMonitorCapabilities::Supports filters against the timing list", "[gfx][monitor]") {
  kernel::KMonitorCapabilities caps{};
  REQUIRE(caps.Add(kernel::KMonitorTiming{.width = 640, .height = 480, .refreshRate = 60}) == kernel::KernelError::Success);
  REQUIRE(caps.Add(kernel::KMonitorTiming{.width = 800, .height = 600, .refreshRate = 0}) == kernel::KernelError::Success);

  kernel::KDisplayModeInfo mode{};
  mode.width = 640;
  mode.height = 480;
  mode.refreshRate = 60;
  REQUIRE(caps.Supports(mode));

  // Listed resolution, unlisted refresh rate.
  mode.refreshRate = 85;
  REQUIRE_FALSE(caps.Supports(mode));

  // A timing with refreshRate 0 accepts any refresh rate.
  mode.width = 800;
  mode.height = 600;
  REQUIRE(caps.Supports(mode));

  // Resolution not in the list at all.
  mode.width = 1024;
  mode.height = 768;
  REQUIRE_FALSE(caps.Supports(mode));
}

TEST_CASE("KMonitorCapabilities Max* limits of zero are unconstrained", "[gfx][monitor]") {
  kernel::KDisplayModeInfo mode{};
  mode.width = 1600;
  mode.height = 1200;
  mode.refreshRate = 85;

  // No limits and no timing list: everything passes.
  const kernel::KMonitorCapabilities unconstrained{};
  REQUIRE(unconstrained.Supports(mode));

  kernel::KMonitorCapabilities limited{};
  limited.maxWidth = 1024;
  REQUIRE_FALSE(limited.Supports(mode));

  limited.maxWidth = 0;
  limited.maxRefreshRate = 60;
  REQUIRE_FALSE(limited.Supports(mode));
}

TEST_CASE("KMonitorCapabilities::BuiltinFallback allows only 640x480@60", "[gfx][monitor]") {
  const auto caps = kernel::KMonitorCapabilities::BuiltinFallback();
  REQUIRE(caps.isDefault);
  REQUIRE(caps.count == 1);

  kernel::KDisplayModeInfo mode{};
  mode.width = 640;
  mode.height = 480;
  mode.refreshRate = 60;
  REQUIRE(caps.Supports(mode));

  mode.width = 800;
  mode.height = 600;
  REQUIRE_FALSE(caps.Supports(mode));
}

TEST_CASE("parse_timing accepts WxH and WxH@R and rejects malformed input", "[gfx][monitor]") {
  auto timing = kernel::gfx::parse_timing("1024x768@75");
  REQUIRE(timing.has_value());
  REQUIRE(timing->width == 1024);
  REQUIRE(timing->height == 768);
  REQUIRE(timing->refreshRate == 75);

  // The refresh suffix is optional and means "any".
  timing = kernel::gfx::parse_timing("640x480");
  REQUIRE(timing.has_value());
  REQUIRE(timing->refreshRate == 0);

  REQUIRE(kernel::gfx::parse_timing("1024x").error() == kernel::KernelError::InvalidFormat);
  REQUIRE(kernel::gfx::parse_timing("abc").error() == kernel::KernelError::InvalidFormat);
  REQUIRE(kernel::gfx::parse_timing("").error() == kernel::KernelError::InvalidFormat);
  REQUIRE(kernel::gfx::parse_timing("1024x768@").error() == kernel::KernelError::InvalidFormat);
  REQUIRE(kernel::gfx::parse_timing("4294967296x1").error() == kernel::KernelError::ArithmeticOverflow);
}

TEST_CASE("capabilities_from_edid derives timings from the EDID standard timing list", "[gfx][monitor]") {
  kstd::array<uint8_t, kernel::Edid::BlockSize> blob{};
  static constexpr uint8_t header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
  for (size_t i = 0; i < 8; ++i) blob[i] = header[i];

  // Mark every standard timing slot unused, then describe one 1024x768@60.
  for (size_t i = 0x26; i <= 0x35; i += 2) {
    blob[i] = 0x01;
    blob[i + 1] = 0x01;
  }
  blob[0x26] = 1024 / 8 - 31;// horizontal active pixels
  blob[0x27] = 0x40;         // 4:3 aspect, 60 Hz

  uint8_t sum = 0;
  for (size_t i = 0; i < kernel::Edid::BlockSize - 1; ++i) sum = static_cast<uint8_t>(sum + blob[i]);
  blob[kernel::Edid::BlockSize - 1] = static_cast<uint8_t>(0u - sum);

  const kernel::Edid edid{kstd::span<const uint8_t>(blob.data(), blob.size())};
  REQUIRE(edid.IsValid());

  const auto caps = kernel::gfx::capabilities_from_edid(edid);
  REQUIRE_FALSE(caps.isDefault);
  REQUIRE(caps.count == 1);
  REQUIRE(caps.timings[0].width == 1024);
  REQUIRE(caps.timings[0].height == 768);
  REQUIRE(caps.timings[0].refreshRate == 60);
  REQUIRE(caps.maxWidth == 1024);

  kernel::KDisplayModeInfo mode{};
  mode.width = 1024;
  mode.height = 768;
  mode.refreshRate = 60;
  REQUIRE(caps.Supports(mode));
}

TEST_CASE("A non-PnP head gets the default monitor capabilities and they refuse outside modes", "[gfx][monitor]") {
  EnsureObjectManagerInitialized();

  ResetFakeAdapterState();
  // The driver reports a second, larger mode the default monitor rejects.
  g_fakeAdapterState.modeCount = 2;

  auto displays = StartFakeAdapter();
  REQUIRE(displays.size() == 1);

  const auto &display = displays[0];
  REQUIRE(display->monitorCaps.isDefault);

  // The driver still reports the mode...
  const auto reported = display->GetModeInfo(display, 1);
  REQUIRE(reported.has_value());
  REQUIRE(reported->width == 800);

  // ...but the monitor capabilities keep the kernel from programming it.
  ResetFakeCalls();
  REQUIRE(KE_GFX_SetDisplayMode(display, 1) == kernel::KernelError::Unsupported);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);

  // The mode the default monitor accepts still programs.
  REQUIRE(KE_GFX_SetDisplayMode(display, 0) == kernel::KernelError::Success);
}
