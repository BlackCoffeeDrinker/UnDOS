#include <catch2/catch_test_macros.hpp>

#include "ob_test_support.hpp"

#include "gfx/gfx.hpp"

#include <kernel/gfx.hpp>
#include <kernel/io.hpp>
#include <kernel/gfx/KDeviceContextTable.hpp>
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KGraphicsAdapter.hpp>
#include <kernel/kobject/KMemoryBackingObject.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/object_manager.hpp>
#include <kernel/virtual_memory.hpp>
#include "../../../Apps/Init/display_config.hpp"
#include "../../../Apps/Init/display_probe.hpp"
#include "../../../Kernel/Drivers/gfx_vga/vga_mode.hpp"
#include "sched_test_hooks.hpp"
#include "sync_test_hooks.hpp"
#include "syscalls/syscalls.hpp"
#include "user/user.hpp"
#include "event_broadcaster.hpp"
#include <undos/syscalls/__syscall_ValidateVideoPresentTopology_V1.h>
#include <undos/syscalls/__syscall_CommitVideoPresentTopology_V1.h>
#include <undos/syscalls/__syscall_EnumVideoPresentTargets_V1.h>
#include <undos/syscalls/__syscall_EnumVideoPresentSourceModes_V1.h>
#include <undos/syscalls/__syscall_EnumVideoPresentTargetModes_V1.h>
#include <undos/syscalls/__syscall_EnumVideoPresentPaths_V1.h>
#include "syscalls/KErrorToSysReturnCode.hpp"

namespace {
constexpr size_t UNDOS_MAX_MAPPED_BUFFER_SIZE = 64 * 1024 * 1024;
size_t dispatchInputCount{};
size_t dispatchInputLength{};
size_t dispatchEmptyMaps{};
void *dispatchInvalidAddress{};
void *dispatchObservedOutput{};
bool dispatchOutputMapped{};
kstd::expected<void *, kstd::error_code> DispatchMap(
    void *address, size_t bytes, kernel::vmm::AddressSpace &) {
  if (!bytes) ++dispatchEmptyMaps;
  if (address == dispatchInvalidAddress)
    return kstd::unexpected(kernel::KernelError::InvalidAddress);
  if (bytes && address == dispatchObservedOutput) dispatchOutputMapped = true;
  return address;
}
kstd::error_code DispatchUnmap(void *, size_t, kernel::vmm::AddressSpace &) { return {}; }
kstd::error_code DispatchCopy(kernel::vmm::AddressSpace &, void *to,
                              const void *from, size_t bytes) {
  kstd::memcpy(to, from, bytes);
  return {};
}
kstd::error_code DispatchValidate(__KRN_GFX_VALIDATEVIDEOPRESENTTOPOLOGY_V1 &&args) {
  dispatchInputCount = args.paths.size();
  dispatchInputLength = args.paths_len;
  const auto error = handle_ValidateVideoPresentTopology_V1(kstd::move(args));
  args.paths.release();
  return error;
}
kstd::error_code DispatchCommit(__KRN_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 &&args) {
  const auto error = handle_CommitVideoPresentTopology_V1(kstd::move(args));
  args.results.release();
  return error;
}
kstd::error_code DispatchTargets(__KRN_GFX_ENUMVIDEOPRESENTTARGETS_V1 &&args) {
  const auto error = handle_EnumVideoPresentTargets_V1(kstd::move(args));
  dispatchOutputMapped = false;
  args.targets.release();
  return error;
}
kstd::error_code DispatchSources(__KRN_GFX_ENUMVIDEOPRESENTSOURCEMODES_V1 &&args) {
  const auto error = handle_EnumVideoPresentSourceModes_V1(kstd::move(args));
  dispatchOutputMapped = false;
  args.modes.release();
  return error;
}
kstd::error_code DispatchTargetModes(__KRN_GFX_ENUMVIDEOPRESENTTARGETMODES_V1 &&args) {
  const auto error = handle_EnumVideoPresentTargetModes_V1(kstd::move(args));
  dispatchOutputMapped = false;
  args.modes.release();
  return error;
}
kstd::error_code DispatchPaths(__KRN_GFX_ENUMVIDEOPRESENTPATHS_V1 &&args) {
  const auto error = handle_EnumVideoPresentPaths_V1(kstd::move(args));
  dispatchOutputMapped = false;
  args.paths.release();
  return error;
}
#define KE_VMM_MapBorrowed DispatchMap
#define KE_VMM_UnmapBorrowed DispatchUnmap
#define KE_VMM_CopyToUserspace DispatchCopy
#define handle_ValidateVideoPresentTopology_V1 DispatchValidate
#define handle_CommitVideoPresentTopology_V1 DispatchCommit
#define handle_EnumVideoPresentTargets_V1 DispatchTargets
#define handle_EnumVideoPresentSourceModes_V1 DispatchSources
#define handle_EnumVideoPresentTargetModes_V1 DispatchTargetModes
#define handle_EnumVideoPresentPaths_V1 DispatchPaths
#include "generated_validate_dispatch.inc"
#include "generated_commit_dispatch.inc"
#include "generated_targets_dispatch.inc"
#include "generated_EnumVideoPresentSourceModes_dispatch.inc"
#include "generated_EnumVideoPresentTargetModes_dispatch.inc"
#include "generated_EnumVideoPresentPaths_dispatch.inc"
#undef handle_EnumVideoPresentPaths_V1
#undef handle_EnumVideoPresentTargetModes_V1
#undef handle_EnumVideoPresentSourceModes_V1
#undef handle_EnumVideoPresentTargets_V1
#undef handle_CommitVideoPresentTopology_V1
#undef handle_ValidateVideoPresentTopology_V1
#undef KE_VMM_CopyToUserspace
#undef KE_VMM_UnmapBorrowed
#undef KE_VMM_MapBorrowed

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
  bool bindValidate = true;
  size_t configureCalls = 0;
  uint32_t lastConfiguredHead = 0xFFFFFFFFu;

  // --- SetMode ---
  bool setModeShouldSucceed = true;
  bool enableShouldSucceed = true;
  bool programFailureChangesMode = false;
  bool bindRestoreMode = true;
  bool validateShouldSucceed = true;
  size_t failEnableCallsRemaining = 0;
  size_t enableCalls = 0;
  size_t prepareCalls = 0;
  size_t finishCalls = 0;
  size_t preparedBackingSize = 0;
  size_t setModeCalls = 0;
  size_t lastSetModeIndex = 0;
  size_t presentCalls = 0;
  size_t removedCallbacks = 0;
  size_t modeCallsUntilResourceChange = 0;

  // Mode list the fake display reports. Mode 0 is the only one the default
  // monitor capabilities accept; mode 1 exercises the runtime switch.
  size_t modeCount = 1;
  kernel::KDisplayModeInfo modes[65]{
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

  // Hook ordering log: 'P' = prepare, 'D' = disable, 'S' = program,
  // 'E' = enable, 'F' = finish reservation.
  char callLog[64]{};
  size_t callLogLen = 0;

  bool fillRectCalled = false;
  bool drawLineCalled = false;
  bool bitBltCalled = false;

  int lastX = 0, lastY = 0, lastW = 0, lastH = 0;
  uint32_t lastColor = 0;
};

FakeAdapterState g_fakeAdapterState;
uint8_t g_fakeFramebuffers[16][800 * 600]{};

void ResetFakeAdapterState() {
  g_fakeAdapterState = FakeAdapterState{};
}

// Clears the recorded calls while keeping the configured behaviour, so a test
// is not confused by whatever the kernel did while starting the adapter.
void ResetFakeCalls() {
  g_fakeAdapterState.configureCalls = 0;
  g_fakeAdapterState.prepareCalls = 0;
  g_fakeAdapterState.finishCalls = 0;
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

kstd::expected<kernel::KDisplayModeInfo, kstd::error_code> FakeGetModeInfo(const kernel::KObjectPtr<kernel::KDisplay> &display, size_t modeIndex) {
  if (g_fakeAdapterState.modeCallsUntilResourceChange &&
      --g_fakeAdapterState.modeCallsUntilResourceChange == 0)
    ++display->adapter->resourceGeneration;
  if (display->adapter && display->adapter->device &&
      display->adapter->device->pnpState == kernel::KPnpState::Removed)
    ++g_fakeAdapterState.removedCallbacks;
  if (modeIndex >= g_fakeAdapterState.modeCount) {
    return kstd::unexpected(kernel::KernelError::OutOfRange);
  }
  kernel::KDisplayModeInfo info = g_fakeAdapterState.modes[modeIndex];
  info.modeIndex = modeIndex;
  return info;
}

size_t observedModeCallbacks{}, modeCallbacksWithSpinlocks{}, modeCallbacksWithOutputMapping{};
kstd::expected<kernel::KDisplayModeInfo, kstd::error_code>
ObserveModeCallback(const kernel::KObjectPtr<kernel::KDisplay> &display, size_t index) {
  ++observedModeCallbacks;
  if (test_sync::held_spinlocks()) ++modeCallbacksWithSpinlocks;
  if (dispatchOutputMapped) ++modeCallbacksWithOutputMapping;
  return FakeGetModeInfo(display, index);
}

kstd::error_code FakePrepareMode(const kernel::KObjectPtr<kernel::KDisplay> &display,
                                 size_t modeIndex,
                                 kernel::KDisplayModeReservation &reservation) {
  RecordCall('P');
  ++g_fakeAdapterState.prepareCalls;
  const auto mode = FakeGetModeInfo(display, modeIndex);
  if (!mode) return mode.error();
  auto backing = KE_VMM_CreateDirectBacking(
      kernel::VirtualAddress::from_ptr(g_fakeFramebuffers[display->headIndex]),
      g_fakeAdapterState.preparedBackingSize
          ? g_fakeAdapterState.preparedBackingSize
          : sizeof(g_fakeFramebuffers[display->headIndex]));
  if (!backing) return backing.error();
  reservation = {};
  reservation.modeIndex = modeIndex;
  reservation.mode = *mode;
  reservation.binding.backing = kstd::move(*backing);
  reservation.binding.type = kernel::SurfaceType::Framebuffer;
  reservation.binding.flags = kernel::SurfaceFlags::TopDown;
  return kernel::KernelError::Success;
}

kstd::error_code FakeProgramMode(const kernel::KObjectPtr<kernel::KDisplay> &display,
                                 kernel::KDisplayModeReservation &reservation) {
  RecordCall('S');
  g_fakeAdapterState.setModeCalls++;
  g_fakeAdapterState.lastSetModeIndex = reservation.modeIndex;
  if (!g_fakeAdapterState.setModeShouldSucceed) {
    if (g_fakeAdapterState.programFailureChangesMode)
      display->currentMode = reservation.mode;
    return kernel::KernelError::DeviceError;
  }
  display->currentMode = reservation.mode;
  if (!display->modeGeneration) display->modeGeneration = 1;
  return kernel::KernelError::Success;
}

void FakeFinishMode(const kernel::KObjectPtr<kernel::KDisplay> &,
                    kernel::KDisplayModeReservation &, bool) {
  RecordCall('F');
  ++g_fakeAdapterState.finishCalls;
}

kstd::error_code FakeValidate(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  return display && display->isPresent && g_fakeAdapterState.validateShouldSucceed
             ? kernel::KernelError::Success
             : kernel::KernelError::DeviceError;
}

kstd::error_code FakeRestoreMode(const kernel::KObjectPtr<kernel::KDisplay> &display,
                                 size_t modeIndex,
                                 kernel::KDisplayModeReservation &) {
  const auto mode = FakeGetModeInfo(display, modeIndex);
  if (!mode) return mode.error();
  display->currentMode = *mode;
  return kernel::KernelError::Success;
}

kstd::expected<kernel::KDisplayActualState, kstd::error_code>
FakeQueryActualState(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  return kernel::KDisplayActualState{
      .known = display->modeGeneration != 0,
      .enabled = display->isAttachedToDesktop,
      .modeIndex = display->currentMode.modeIndex};
}

size_t observedActualCallbacks{}, ungatedActualCallbacks{};
kstd::expected<kernel::KDisplayActualState, kstd::error_code>
ObserveActualCallback(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  ++observedActualCallbacks;
  if (!kernel::gfx::video_present_test_callback_gate_held()) ++ungatedActualCallbacks;
  return FakeQueryActualState(display);
}

kstd::error_code FakeEnable(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  RecordCall('E');
  ++g_fakeAdapterState.enableCalls;
  if (g_fakeAdapterState.failEnableCallsRemaining) {
    --g_fakeAdapterState.failEnableCallsRemaining;
    return kernel::KernelError::DeviceError;
  }
  if (!g_fakeAdapterState.enableShouldSucceed) return kernel::KernelError::DeviceError;
  display->isAttachedToDesktop = true;
  return kernel::KernelError::Success;
}

kstd::error_code FakeDisable(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  RecordCall('D');
  display->isAttachedToDesktop = false;
  return kernel::KernelError::Success;
}

kstd::error_code FakeProbeDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                                     const kernel::KGraphicsAdapterDisplayEnumInfo &enumInfo,
                                     kernel::KGraphicsAdapterDisplayInfo &outInfo) {
  if (adapter->device && adapter->device->pnpState == kernel::KPnpState::Removed)
    ++g_fakeAdapterState.removedCallbacks;
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
    display->PrepareMode = FakePrepareMode;
    display->ProgramMode = FakeProgramMode;
    display->FinishMode = FakeFinishMode;
    if (g_fakeAdapterState.bindRestoreMode)
      display->RestoreMode = FakeRestoreMode;
    display->QueryActualState = FakeQueryActualState;
  }
  display->Enable = FakeEnable;
  display->Disable = FakeDisable;
  if (g_fakeAdapterState.bindValidate) display->Validate = FakeValidate;
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

  auto device = KE_IO_CreateDevice(nullptr, 0, adapterName, kernel::device_type::Video);
  REQUIRE(device);
  auto registered = KE_GFX_RegisterGraphicsAdapter(nullptr, *device, adapterName);
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

struct TargetIdIsolation {
  kernel::KObjectPtr<kernel::KDisplay> priorTargets[64]{};
  uint32_t priorIds[64]{};
  size_t priorCount{};
  kernel::KObjectPtr<kernel::KDisplay> createdTargets[65]{};
  size_t createdCount{};
  bool overflow{};

  TargetIdIsolation() {
    KE_OB_GetGfxDirectory()->Each<kernel::KDisplay>(
        [&](const kernel::KObjectPtr<kernel::KDisplay> &target) {
          if (!target->videoPresentTargetId) return;
          if (priorCount == 64) {
            overflow = true;
            return;
          }
          priorTargets[priorCount] = target;
          priorIds[priorCount++] = target->videoPresentTargetId;
          target->videoPresentTargetId = 0;
        });
  }

  void Track(const kstd::vector<kernel::KObjectPtr<kernel::KDisplay>> &targets) {
    for (const auto &target : targets) {
      if (createdCount == 65) {
        overflow = true;
        return;
      }
      createdTargets[createdCount++] = target;
    }
  }

  void Track(const kernel::KObjectPtr<kernel::KDisplay> &target) {
    if (createdCount == 65) {
      overflow = true;
      return;
    }
    createdTargets[createdCount++] = target;
  }

  ~TargetIdIsolation() {
    for (size_t i = 0; i < createdCount; ++i)
      createdTargets[i]->videoPresentTargetId = 0;
    for (size_t i = 0; i < priorCount; ++i)
      priorTargets[i]->videoPresentTargetId = priorIds[i];
  }
};

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

kstd::expected<kernel::gfx::VideoPresentSourceModeInfo, kstd::error_code>
MatchingSourceMode(uint32_t generation, const kernel::KDisplayModeInfo &mode) {
  kernel::gfx::VideoPresentTopologyInfo topology{};
  if (const auto error = kernel::gfx::get_video_present_topology(topology); error)
    return kstd::unexpected(error);
  for (uint32_t index = 0; index < topology.sourceModeCount; ++index) {
    const auto source = kernel::gfx::video_present_source_mode(
        generation, kernel::gfx::DesktopSourceId, index);
    if (!source) return kstd::unexpected(source.error());
    if (source->width == mode.width && source->height == mode.height &&
        source->colorDepth == mode.bpp && source->pixelFormat == mode.pixelFormat)
      return source;
  }
  return kstd::unexpected(kernel::KernelError::NotFound);
}

kstd::expected<kernel::gfx::VideoPresentTargetResult, kstd::error_code>
SetModeViaTopology(const kernel::KObjectPtr<kernel::KDisplay> &target, uint32_t modeOrdinal) {
  kernel::gfx::VideoPresentTopologyInfo topology{};
  if (const auto error = kernel::gfx::get_video_present_topology(topology); error)
    return kstd::unexpected(error);
  const auto targetMode = kernel::gfx::video_present_target_mode(
      topology.generation, target->videoPresentTargetId, modeOrdinal);
  if (!targetMode) return kstd::unexpected(targetMode.error());
  kernel::gfx::VideoPresentSourceModeInfo sourceMode{};
  bool found = false;
  for (uint32_t index = 0; index < topology.sourceModeCount; ++index) {
    const auto candidate = kernel::gfx::video_present_source_mode(
        topology.generation, kernel::gfx::DesktopSourceId, index);
    if (!candidate) return kstd::unexpected(candidate.error());
    if (candidate->width == targetMode->mode.width &&
        candidate->height == targetMode->mode.height &&
        candidate->colorDepth == targetMode->mode.bpp &&
        candidate->pixelFormat == targetMode->mode.pixelFormat) {
      sourceMode = *candidate;
      found = true;
      break;
    }
  }
  if (!found) return kstd::unexpected(kernel::KernelError::Unsupported);
  const kernel::gfx::VideoPresentPathInfo path{
      .sourceId = kernel::gfx::DesktopSourceId,
      .sourceModeId = sourceMode.modeId,
      .targetId = targetMode->targetId,
      .targetModeId = targetMode->modeId,
      .sourceX = 0,
      .sourceY = 0,
      .width = targetMode->mode.width,
      .height = targetMode->mode.height,
      .flags = 0};
  kernel::KObjectPtr<kernel::KDisplay> preparedTarget;
  size_t modeIndex = 0;
  kernel::KDisplayModeReservation reservation{};
  if (const auto error = kernel::gfx::video_present_prepare_path(
          topology.generation, path, preparedTarget, modeIndex, reservation);
      error)
    return kstd::unexpected(error);
  return kernel::gfx::commit_video_present_path(
      topology.generation, path, preparedTarget, reservation);
}

}// namespace

TEST_CASE("Basic VGA exposes only its fixed four-plane mode", "[gfx][vga]") {
  kernel::KDisplayModeInfo mode{};
  REQUIRE(kernel::gfx::vga::GetModeInfo(0, mode));
  REQUIRE(mode.modeIndex == 0);
  REQUIRE(mode.width == 640);
  REQUIRE(mode.height == 480);
  REQUIRE(mode.pitch == 80);
  REQUIRE(mode.bpp == 4);
  REQUIRE(mode.numberOfPlanes == 4);
  REQUIRE(mode.bitsPerPlane == 1);
  REQUIRE(mode.refreshRate == 60);
  REQUIRE(mode.pixelFormat == kernel::PixelFormat::INDEX8);
  REQUIRE(mode.attributeFlags == (kernel::DisplayModeFlags::GraphicsMode |
                                  kernel::DisplayModeFlags::ColorMode |
                                  kernel::DisplayModeFlags::PaletteDriven));
  REQUIRE_FALSE(kernel::gfx::vga::GetModeInfo(1, mode));
}

TEST_CASE("KE_GFX_CreateSurface populates surface shape from mode info on success", "[gfx]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  const auto result = SetModeViaTopology(display, 0);
  const auto surface = display->surface;

  REQUIRE(result);
  REQUIRE(result->outcome == 0);
  REQUIRE(surface);
  REQUIRE(surface->width == 640);
  REQUIRE(surface->height == 480);
  REQUIRE(surface->pitch == 80);
  REQUIRE(surface->pixelFormat == kernel::PixelFormat::INDEX8);
  REQUIRE(g_fakeAdapterState.lastSetModeIndex == 0);
  REQUIRE(kernel::gfx::active_video_present_target() == display);
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.pathCount == 1);
  const auto activePath = kernel::gfx::video_present_path(topology.generation, 0);
  REQUIRE(activePath);
  REQUIRE(activePath->targetId == display->videoPresentTargetId);
  kernel::KObjectPtr<kernel::gfx::KVideoPresentSource> source;
  KE_OB_GetGfxDirectory()->Each<kernel::gfx::KVideoPresentSource>(
      [&](const auto &candidate) { source = candidate; });
  REQUIRE(source);
  REQUIRE(source->active);
  REQUIRE(source->target == display);
  REQUIRE(source->surface == surface);
  REQUIRE(source->currentMode.modeIndex == display->currentMode.modeIndex);
}

TEST_CASE("KE_GFX_CreateSurface rejects an out-of-range modeIndex without calling SetMode", "[gfx]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  const auto result = SetModeViaTopology(display, 1);

  REQUIRE_FALSE(result);
  REQUIRE(result.error() == kernel::KernelError::NotFound);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
}

TEST_CASE("KE_GFX_CreateSurface returns null instead of crashing when no driver is bound", "[gfx]") {
  EnsureObjectManagerInitialized();

  ResetFakeAdapterState();
  // ConfigureDisplay deliberately leaves the target transaction callbacks unset.
  g_fakeAdapterState.bindSetMode = false;

  auto displays = StartFakeAdapter();
  REQUIRE(displays.size() == 1);
  REQUIRE_FALSE(displays[0]->PrepareMode);

  const auto result = SetModeViaTopology(displays[0], 0);
  REQUIRE_FALSE(result);
  REQUIRE(result.error() == kernel::KernelError::NotFound);
}

TEST_CASE("KE_GFX_CreateSurface returns null when the driver's SetMode fails", "[gfx]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.setModeShouldSucceed = false;

  const auto result = SetModeViaTopology(display, 0);
  REQUIRE(result);
  REQUIRE(result->outcome == 3);
  REQUIRE_FALSE(display->surface);
}

TEST_CASE("Target validation failure stops before resource reservation", "[gfx][topology]") {
  EnsureObjectManagerInitialized();
  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.validateShouldSucceed = false;

  const auto result = SetModeViaTopology(display, 0);
  REQUIRE_FALSE(result);
  REQUIRE(result.error() == kernel::KernelError::DeviceError);
  REQUIRE(g_fakeAdapterState.prepareCalls == 0);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
}

TEST_CASE("Target reservation rejects an undersized backing before programming", "[gfx][topology]") {
  EnsureObjectManagerInitialized();
  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.preparedBackingSize = 1;

  const auto result = SetModeViaTopology(display, 0);
  REQUIRE_FALSE(result);
  REQUIRE(result.error() == kernel::KernelError::InvalidState);
  REQUIRE(g_fakeAdapterState.prepareCalls == 1);
  REQUIRE(g_fakeAdapterState.finishCalls == 1);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
  REQUIRE(CallLog() == "PF");
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

  REQUIRE_FALSE(table.Close(hdc));
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

  REQUIRE_FALSE(table.Close(hdcA));

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
  const auto modeResult = SetModeViaTopology(display, 0);
  const auto surface = display->surface;
  REQUIRE(modeResult);
  REQUIRE(surface);

  REQUIRE_FALSE(display->adapter->FillRect(display->adapter, surface, 1, 2, 3, 4, 9));
  REQUIRE(g_fakeAdapterState.fillRectCalled);
  REQUIRE(g_fakeAdapterState.lastX == 1);
  REQUIRE(g_fakeAdapterState.lastY == 2);
  REQUIRE(g_fakeAdapterState.lastW == 3);
  REQUIRE(g_fakeAdapterState.lastH == 4);
  REQUIRE(g_fakeAdapterState.lastColor == 9);

  REQUIRE_FALSE(display->adapter->DrawLine(display->adapter, surface, 5, 6, 7, 8, 3));
  REQUIRE(g_fakeAdapterState.drawLineCalled);
  REQUIRE(g_fakeAdapterState.lastX == 5);
  REQUIRE(g_fakeAdapterState.lastY == 6);
  REQUIRE(g_fakeAdapterState.lastColor == 3);

  REQUIRE_FALSE(display->adapter->BitBlt(display->adapter, surface, 10, 11, surface, 0, 0, 4, 4, kernel::RasterOp::SRCCOPY));
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

  REQUIRE_FALSE(KE_GFX_SoftwareFillRect(surface, 0, 0, 4, 4, 0x42));
  auto *pixels = surface->backing->directAddress.as_ptr<uint8_t>();
  for (uint32_t y = 0; y < 4; ++y) {
    for (uint32_t x = 0; x < 4; ++x) {
      REQUIRE(pixels[y * width + x] == 0x42);
    }
  }

  REQUIRE_FALSE(KE_GFX_SoftwareDrawLine(surface, 0, 0, 7, 0, 0x11));
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

  REQUIRE_FALSE(KE_GFX_SoftwareBitBlt(dst, 0, 0, surface, 0, 0, 4, 4, kernel::RasterOp::SRCCOPY));
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
  REQUIRE(SetModeViaTopology(display, 0));
  slot->target = display->surface;
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

  REQUIRE_FALSE(KE_GFX_SoftwareBitBlt(surface, 1, 1, surface, 0, 0, 7, 7, kernel::RasterOp::SRCCOPY));
  for (uint32_t y = 1; y < 8; ++y)
    for (uint32_t x = 1; x < 8; ++x) REQUIRE(pixels[y * 8 + x] == (y - 1) * 8 + x - 1);

  for (uint32_t i = 0; i < 64; ++i) pixels[i] = static_cast<uint8_t>(i);
  REQUIRE_FALSE(KE_GFX_SoftwareBitBlt(surface, -2, 0, surface, 1, 0, 8, 8, kernel::RasterOp::SRCCOPY));
  for (uint32_t y = 0; y < 8; ++y)
    for (uint32_t x = 0; x < 5; ++x) REQUIRE(pixels[y * 8 + x] == y * 8 + x + 3);

  REQUIRE_FALSE(KE_GFX_SoftwareFillRect(surface, -20, -20, 22, 22, 0xee));
  REQUIRE(pixels[0] == 0xee);
  REQUIRE(pixels[9] == 0xee);
  REQUIRE(pixels[2] != 0xee);
  REQUIRE(pixels[16] != 0xee);
  REQUIRE_FALSE(KE_GFX_SoftwareDrawLine(surface, INT32_MIN, 7, INT32_MAX, 7, 0xcc));
  for (uint32_t x = 0; x < 8; ++x) REQUIRE(pixels[56 + x] == 0xcc);

  surface->type = kernel::SurfaceType::Framebuffer;
  surface->flags = kernel::SurfaceFlags::TopDown;
  REQUIRE(KE_GFX_SoftwareFillRect(surface, 0, 0, 8, 8, 0xff));
  REQUIRE(KE_GFX_SoftwareBitBlt(surface, 0, 0, surface, 0, 0, 8, 8, kernel::RasterOp::SRCCOPY));
  REQUIRE(pixels[0] == 0xee);

  surface->type = kernel::SurfaceType::OffscreenBitmap;
  surface->backing->size = 63;
  REQUIRE(KE_GFX_SoftwareFillRect(surface, 0, 0, 1, 1, 0xff));
  surface->backing->size = 64;
  surface->pitch = -8;
  surface->backing->directAddress = kernel::VirtualAddress::from_ptr(pixels + 56);
  REQUIRE_FALSE(KE_GFX_SoftwareFillRect(surface, 0, 0, 2, 2, 0xab));
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
  REQUIRE_FALSE(KE_GFX_SoftwareFillRect(src, 0, 0, 4, 2, 0xF81F));

  auto *srcPixels = src->backing->directAddress.as_ptr<uint16_t>();
  REQUIRE(srcPixels[0] == 0xF81F);
  REQUIRE(srcPixels[3] == 0xF81F);
  // The fifth pixel of row 0 is outside the filled rect, proving the two-byte stride.
  REQUIRE(srcPixels[4] == 0);
  REQUIRE(srcPixels[pitch / 2] == 0xF81F);

  // Same-format blit takes the memcpy fast path.
  const auto dst = make(kernel::PixelFormat::RGB16, "Rgb16Dst");
  REQUIRE_FALSE(KE_GFX_SoftwareBitBlt(dst, 1, 1, src, 0, 0, 4, 2, kernel::RasterOp::SRCCOPY));
  auto *dstPixels = dst->backing->directAddress.as_ptr<uint16_t>();
  REQUIRE(dstPixels[pitch / 2 + 1] == 0xF81F);
  REQUIRE(dstPixels[pitch / 2 + 0] == 0);

  // Direct RGB magenta quantizes to the closest canonical VGA palette color.
  const auto index = make(kernel::PixelFormat::INDEX8, "Index8Dst");
  index->pitch = width;
  REQUIRE_FALSE(KE_GFX_SoftwareBitBlt(index, 0, 0, src, 0, 0, 4, 1, kernel::RasterOp::SRCCOPY));
  auto *indexPixels = index->backing->directAddress.as_ptr<uint8_t>();
  REQUIRE(indexPixels[0] == 13);
  REQUIRE_FALSE(KE_GFX_SoftwareFillRect(index, 0, 0, 2, 1, 1));
  REQUIRE_FALSE(KE_GFX_SoftwareBitBlt(dst, 0, 0, index, 0, 0, 2, 1, kernel::RasterOp::SRCCOPY));
  REQUIRE(dstPixels[0] == 0x0015);// Palette blue = 0x0000aa, not raw integer 1.
  index->type = kernel::SurfaceType::Framebuffer;
  index->flags = kernel::SurfaceFlags::Linear;
  REQUIRE_FALSE(KE_GFX_SoftwareFillRect(index, 0, 0, 2, 1, 2));// Cirrus 3:3:2 blue.
  REQUIRE_FALSE(KE_GFX_SoftwareBitBlt(dst, 0, 0, index, 0, 0, 2, 1, kernel::RasterOp::SRCCOPY));
  REQUIRE(dstPixels[0] == 0x0015);

  // RGB15 shares the two-byte layout.
  const auto rgb15 = make(kernel::PixelFormat::RGB15, "Rgb15Dst");
  REQUIRE_FALSE(KE_GFX_SoftwareFillRect(rgb15, 2, 0, 1, 1, 0x7C1F));
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
    REQUIRE(display->PrepareMode);
    REQUIRE(display->ProgramMode);
    REQUIRE(display->FinishMode);
    REQUIRE(display->QueryActualState);
    REQUIRE(display->isPresent);
  }
  REQUIRE(sawHead0);
  REQUIRE(sawHead1);

  // Some display has been elected primary by the subsystem.
  REQUIRE(kernel::gfx::has_video_present_targets());
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

TEST_CASE("StartAdapter retains disconnected targets with stable identities", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  ResetFakeAdapterState();
  g_fakeAdapterState.headCount = 2;
  g_fakeAdapterState.firstHeadAbsent = true;

  auto displays = StartFakeAdapter();
  REQUIRE(displays.size() == 2);
  bool sawDisconnectedHead = false;
  bool sawConnectedHead = false;
  for (const auto &display : displays) {
    REQUIRE(display->videoPresentTargetId != 0);
    if (display->headIndex == 0) {
      REQUIRE_FALSE(display->isPresent);
      sawDisconnectedHead = true;
    } else if (display->headIndex == 1) {
      REQUIRE(display->isPresent);
      sawConnectedHead = true;
    }
  }
  REQUIRE(sawDisconnectedHead);
  REQUIRE(sawConnectedHead);
  REQUIRE(g_fakeAdapterState.configureCalls == 2);
  REQUIRE(g_fakeAdapterState.lastConfiguredHead == 1);
  REQUIRE(kernel::gfx::has_video_present_targets());
}

TEST_CASE("Disconnected-only topology does not count as a usable display", "[gfx][mode]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  REQUIRE_FALSE(isolation.overflow);
  ResetFakeAdapterState();
  g_fakeAdapterState.firstHeadAbsent = true;

  const auto displays = StartFakeAdapter();
  isolation.Track(displays);
  REQUIRE_FALSE(isolation.overflow);
  REQUIRE(displays.size() == 1);
  REQUIRE_FALSE(displays[0]->isPresent);
  REQUIRE(SetModeViaTopology(displays[0], 0).error() ==
          kernel::KernelError::Unsupported);
  REQUIRE(g_fakeAdapterState.prepareCalls == 0);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
  REQUIRE_FALSE(kernel::gfx::has_video_present_targets());
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.targetCount == 1);
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
  REQUIRE_FALSE(display->monitorCaps.Add({800, 600, 60}));
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

TEST_CASE("Topology syscall validation snapshots input and commits one path", "[gfx][topology]") {
  EnsureObjectManagerInitialized();
  const auto display = MakeFakeDisplay();
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  const auto mode = kernel::gfx::video_present_target_mode(
      topology.generation, display->videoPresentTargetId, 0);
  REQUIRE(mode);
  const auto source = MatchingSourceMode(topology.generation, mode->mode);
  REQUIRE(source);
  VideoPresentPath path{
      .sourceId = kernel::gfx::DesktopSourceId,
      .sourceModeId = source->modeId,
      .targetId = display->videoPresentTargetId,
      .targetModeId = mode->modeId,
      .sourceX = 0,
      .sourceY = 0,
      .width = mode->mode.width,
      .height = mode->mode.height,
      .flags = 0};
  auto process = kernel::CreateKObject<kernel::KProcessObject>("video-present-process").value();
  auto otherProcess = kernel::CreateKObject<kernel::KProcessObject>("other-video-process").value();
  auto thread = kernel::CreateKObject<kernel::KThreadObject>("video-present-thread").value();
  thread->owner_process = process.get();
  test_sched::SetCurrentThread(thread);

  for (const uint32_t count : {0u, 2u, 64u, 65u}) {
    __KRN_GFX_VALIDATEVIDEOPRESENTTOPOLOGY_V1 invalid{};
    invalid.generation = topology.generation;
    invalid.paths = kernel::borrowed_ptr<VideoPresentPath>(&path, count, {});
    invalid.paths_len = count;
    REQUIRE(handle_ValidateVideoPresentTopology_V1(kstd::move(invalid)) ==
            (count > kernel::gfx::VideoPresentRecordLimit
                 ? kernel::KernelError::ResourceLimitExceeded
                 : kernel::KernelError::InvalidSize));
    invalid.paths.release();
    REQUIRE(invalid.token == 0);
    REQUIRE(g_fakeAdapterState.prepareCalls == 0);
  }

  __SYS_GFX_VALIDATEVIDEOPRESENTTOPOLOGY_V1 validate{};
  validate.generation = topology.generation;
  validate.paths = &path;
  validate.pathsLength = 1;
  REQUIRE_FALSE(Dispatch_ValidateVideoPresentTopology_V1(&validate, sizeof(validate), 0));
  REQUIRE(dispatchInputCount == 1);
  REQUIRE(dispatchInputLength == 1);
  const auto token = validate.token;
  REQUIRE(g_fakeAdapterState.prepareCalls == 1);
  REQUIRE(g_fakeAdapterState.finishCalls == 0);
  REQUIRE(process->handles.Lookup(static_cast<kernel::KHandle>(token)));
  path.flags = UINT32_MAX;

  VideoPresentTargetResult result{};
  __KRN_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 tooSmall{};
  tooSmall.token = token;
  tooSmall.results_len = 0;
  const auto setModeCallsBeforePreflight = g_fakeAdapterState.setModeCalls;
  REQUIRE(handle_CommitVideoPresentTopology_V1(kstd::move(tooSmall)) ==
          kernel::KernelError::BufferTooSmall);
  REQUIRE(g_fakeAdapterState.setModeCalls == setModeCallsBeforePreflight);
  REQUIRE(g_fakeAdapterState.finishCalls == 0);

  __KRN_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 crossProcess{};
  crossProcess.token = token;
  crossProcess.results = kernel::borrowed_ptr<VideoPresentTargetResult>(&result, 1, {});
  crossProcess.results_len = 1;
  auto otherThread = kernel::CreateKObject<kernel::KThreadObject>("other-video-thread").value();
  otherThread->owner_process = otherProcess.get();
  test_sched::SetCurrentThread(otherThread);
  REQUIRE(handle_CommitVideoPresentTopology_V1(kstd::move(crossProcess)) ==
          kernel::KernelError::InvalidHandle);
  crossProcess.results.release();

  test_sched::SetCurrentThread(thread);
  __KRN_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 commit{};
  commit.token = token;
  commit.results = kernel::borrowed_ptr<VideoPresentTargetResult>(&result, 1, {});
  commit.results_len = 1;
  REQUIRE_FALSE(handle_CommitVideoPresentTopology_V1(kstd::move(commit)));
  commit.results.release();
  REQUIRE(commit.written == 1);
  REQUIRE(result.targetId == display->videoPresentTargetId);
  REQUIRE(result.outcome == VideoPresentOutcome_Success);
  REQUIRE(result.active == 1);
  REQUIRE(g_fakeAdapterState.finishCalls == 1);
  REQUIRE(display->surface);

  __KRN_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 consumed{};
  consumed.token = token;
  consumed.results = kernel::borrowed_ptr<VideoPresentTargetResult>(&result, 1, {});
  consumed.results_len = 1;
  REQUIRE(handle_CommitVideoPresentTopology_V1(kstd::move(consumed)) ==
          kernel::KernelError::InvalidHandle);
  consumed.results.release();
  REQUIRE(g_fakeAdapterState.finishCalls == 1);
  test_sched::SetCurrentThread({});
}

TEST_CASE("Generated commit preserves zero-capacity tokens and validates nonempty mappings",
          "[gfx][topology][dispatch]") {
  EnsureObjectManagerInitialized();
  const auto display = MakeFakeDisplay();
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  const auto mode = kernel::gfx::video_present_target_mode(
      topology.generation, display->videoPresentTargetId, 0);
  REQUIRE(mode);
  const auto source = MatchingSourceMode(topology.generation, mode->mode);
  REQUIRE(source);
  const VideoPresentPath path{
      .sourceId = kernel::gfx::DesktopSourceId, .sourceModeId = source->modeId,
      .targetId = display->videoPresentTargetId, .targetModeId = mode->modeId,
      .width = mode->mode.width, .height = mode->mode.height};
  auto process = kernel::CreateAnonymousKObject<kernel::KProcessObject>().value();
  auto thread = kernel::CreateAnonymousKObject<kernel::KThreadObject>().value();
  thread->owner_process = process.get();
  test_sched::SetCurrentThread(thread);
  __SYS_GFX_VALIDATEVIDEOPRESENTTOPOLOGY_V1 validate{};
  validate.generation = topology.generation;
  validate.paths = &path;
  validate.pathsLength = 1;
  REQUIRE_FALSE(Dispatch_ValidateVideoPresentTopology_V1(&validate, sizeof(validate), 0));
  const auto handle = static_cast<kernel::KHandle>(validate.token);
  bool stale = false;
  SECTION("Current token") {}
  SECTION("Stale token takes precedence over insufficient capacity") {
    REQUIRE_FALSE(KE_GFX_RefreshAdapter(display->adapter));
    stale = true;
  }
  ResetFakeCalls();
  VideoPresentTargetResult result{};
  result.targetId = UINT32_MAX;
  __SYS_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 commit{};
  commit.token = validate.token;
  dispatchEmptyMaps = 0;
  if (stale) {
    commit.results = &result;
    commit.resultsLength = 0;
    REQUIRE(Dispatch_CommitVideoPresentTopology_V1(&commit, sizeof(commit), 0) ==
            kernel::KernelError::Retry);
    REQUIRE(commit.required == 1);
    REQUIRE(commit.written == 0);
    REQUIRE_FALSE(process->handles.Lookup(handle));
    REQUIRE(g_fakeAdapterState.setModeCalls == 0);
    REQUIRE(g_fakeAdapterState.finishCalls == 1);
    REQUIRE(result.targetId == UINT32_MAX);
    test_sched::SetCurrentThread({});
    return;
  }
  for (auto *pointer : {static_cast<VideoPresentTargetResult *>(nullptr), &result}) {
    commit.results = pointer;
    commit.resultsLength = 0;
    commit.written = UINT32_MAX;
    const auto error = Dispatch_CommitVideoPresentTopology_V1(&commit, sizeof(commit), 0);
    REQUIRE(kernel::KErrorToSysReturnCode(error) == SyscallReturnCode_INVALID_SIZE);
    REQUIRE(commit.written == 0);
    REQUIRE(process->handles.Lookup(handle));
    REQUIRE(result.targetId == UINT32_MAX);
    REQUIRE(CallLog().empty());
  }
  REQUIRE(dispatchEmptyMaps == 0);
  commit.results = &result;
  commit.resultsLength = 1;
  dispatchInvalidAddress = &result;
  const auto mappingError = Dispatch_CommitVideoPresentTopology_V1(&commit, sizeof(commit), 0);
  dispatchInvalidAddress = nullptr;
  REQUIRE(mappingError == kernel::KernelError::InvalidAddress);
  REQUIRE(process->handles.Lookup(handle));
  REQUIRE(CallLog().empty());
  const auto error = Dispatch_CommitVideoPresentTopology_V1(&commit, sizeof(commit), 0);
  if (stale) {
    REQUIRE(error == kernel::KernelError::Retry);
    REQUIRE(commit.written == 0);
    REQUIRE(g_fakeAdapterState.setModeCalls == 0);
  } else {
    REQUIRE_FALSE(error);
    REQUIRE(commit.written == 1);
    REQUIRE(result.active == 1);
    REQUIRE(g_fakeAdapterState.setModeCalls == 1);
  }
  REQUIRE(g_fakeAdapterState.finishCalls == 1);
  REQUIRE_FALSE(process->handles.Lookup(handle));
  test_sched::SetCurrentThread({});
}

TEST_CASE("Generated commit serializes stale precedence after snapshot acquisition",
          "[gfx][topology][dispatch][commit-review]") {
  EnsureObjectManagerInitialized();
  auto process = kernel::CreateAnonymousKObject<kernel::KProcessObject>().value();
  auto thread = kernel::CreateAnonymousKObject<kernel::KThreadObject>().value();
  thread->owner_process = process.get();
  test_sched::SetCurrentThread(thread);
  struct ResetThread {
    ~ResetThread() {
      kernel::gfx::video_present_test_before_commit_decision(nullptr);
      test_sched::SetCurrentThread({});
    }
  } resetThread;
  TargetIdIsolation isolation;
  static kernel::KObjectPtr<kernel::KDisplay> display;
  display = MakeFakeDisplay();
  isolation.Track(display);
  const auto snapshot = kernel::gfx::acquire_video_present_snapshot();
  REQUIRE(snapshot);
  const auto &mode = (*snapshot)->targetModes[0];
  VideoPresentPath path{
      .sourceId = kernel::gfx::DesktopSourceId, .sourceModeId = 1,
      .targetId = mode.targetId, .targetModeId = mode.modeId,
      .width = mode.mode.width, .height = mode.mode.height};
  __SYS_GFX_VALIDATEVIDEOPRESENTTOPOLOGY_V1 validate{};
  validate.generation = (*snapshot)->topology.generation;
  validate.paths = &path;
  validate.pathsLength = 1;
  REQUIRE_FALSE(Dispatch_ValidateVideoPresentTopology_V1(&validate, sizeof(validate), 0));
  ResetFakeCalls();
  bool invalidate = false;
  SECTION("Current capacity decision preserves reservation") {}
  SECTION("Refresh wins before the serialized capacity decision") { invalidate = true; }
  SECTION("Removal wins before the serialized capacity decision") {
    invalidate = true;
    g_fakeAdapterState.firstHeadAbsent = true;
  }
  static bool ran, refreshed;
  ran = refreshed = false;
  if (invalidate) {
    const bool remove = g_fakeAdapterState.firstHeadAbsent;
    if (remove) {
      kernel::gfx::video_present_test_before_commit_decision([] {
        ran = true;
        KE_GFX_RemoveAdapter(display->adapter);
        refreshed = true;
      });
    } else {
      kernel::gfx::video_present_test_before_commit_decision([] {
        ran = true;
        refreshed = !KE_GFX_RefreshAdapter(display->adapter);
      });
    }
  }
  VideoPresentTargetResult untouched{};
  untouched.targetId = UINT32_MAX;
  __SYS_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 commit{};
  commit.token = validate.token;
  commit.results = &untouched;
  commit.resultsLength = 0;
  const auto error = Dispatch_CommitVideoPresentTopology_V1(&commit, sizeof(commit), 0);
  REQUIRE(error == (invalidate ? kernel::KernelError::Retry
                              : kernel::KernelError::BufferTooSmall));
  REQUIRE(commit.required == 1);
  REQUIRE(commit.written == 0);
  REQUIRE(untouched.targetId == UINT32_MAX);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
  const auto handle = static_cast<kernel::KHandle>(validate.token);
  if (invalidate) {
    REQUIRE(ran);
    REQUIRE(refreshed);
    REQUIRE_FALSE(process->handles.Lookup(handle));
    REQUIRE(g_fakeAdapterState.finishCalls == 1);
  } else {
    REQUIRE(process->handles.Lookup(handle));
    REQUIRE(g_fakeAdapterState.finishCalls == 0);
    REQUIRE(CallLog().empty());
    REQUIRE_FALSE(process->handles.Close(handle));
    REQUIRE(g_fakeAdapterState.finishCalls == 1);
  }
  display = nullptr;
}

TEST_CASE("Commit prior activation and active-state callbacks use the transaction gate",
          "[gfx][topology][dispatch][commit-review]") {
  EnsureObjectManagerInitialized();
  auto process = kernel::CreateAnonymousKObject<kernel::KProcessObject>().value();
  auto thread = kernel::CreateAnonymousKObject<kernel::KThreadObject>().value();
  thread->owner_process = process.get();
  test_sched::SetCurrentThread(thread);
  struct ResetThread { ~ResetThread() { test_sched::SetCurrentThread({}); } } resetThread;
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  isolation.Track(display);
  g_fakeAdapterState.modeCount = 2;
  REQUIRE(SetModeViaTopology(display, 0));
  display->QueryActualState = ObserveActualCallback;
  observedActualCallbacks = ungatedActualCallbacks = 0;
  struct RestoreCallback {
    kernel::KObjectPtr<kernel::KDisplay> target;
    ~RestoreCallback() { target->QueryActualState = FakeQueryActualState; }
  } restore{display};
  const auto snapshot = kernel::gfx::acquire_video_present_snapshot();
  REQUIRE(snapshot);
  const auto mode = kernel::gfx::video_present_target_mode(
      (*snapshot)->topology.generation, display->videoPresentTargetId, 1);
  REQUIRE(mode);
  const auto source = MatchingSourceMode((*snapshot)->topology.generation, mode->mode);
  REQUIRE(source);
  VideoPresentPath path{
      .sourceId = kernel::gfx::DesktopSourceId, .sourceModeId = source->modeId,
      .targetId = mode->targetId, .targetModeId = mode->modeId,
      .width = mode->mode.width, .height = mode->mode.height};
  __SYS_GFX_VALIDATEVIDEOPRESENTTOPOLOGY_V1 validate{};
  validate.generation = (*snapshot)->topology.generation;
  validate.paths = &path;
  validate.pathsLength = 1;
  REQUIRE_FALSE(Dispatch_ValidateVideoPresentTopology_V1(&validate, sizeof(validate), 0));
  VideoPresentTargetResult result{};
  __SYS_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 commit{};
  commit.token = validate.token;
  commit.results = &result;
  commit.resultsLength = 1;
  REQUIRE_FALSE(Dispatch_CommitVideoPresentTopology_V1(&commit, sizeof(commit), 0));
  REQUIRE(result.active == 1);
  REQUIRE(result.outcome == VideoPresentOutcome_Success);
  REQUIRE(observedActualCallbacks > 0);
  REQUIRE(ungatedActualCallbacks == 0);
  REQUIRE(kernel::gfx::active_video_present_target() == display);
  const auto current = kernel::gfx::acquire_video_present_snapshot();
  REQUIRE(current);
  const auto activePath = kernel::gfx::video_present_path((*current)->topology.generation, 0);
  REQUIRE(activePath);
  kernel::KDisplayModeReservation reservation{};
  kernel::KObjectPtr<kernel::KDisplay> reservedTarget;
  size_t modeIndex{};
  REQUIRE_FALSE(kernel::gfx::video_present_prepare_path(
      (*current)->topology.generation, *activePath, reservedTarget, modeIndex, reservation));
  bool firstActivation = true;
  REQUIRE(kernel::gfx::commit_video_present_path(
      (*current)->topology.generation, *activePath, reservedTarget,
      reservation, 1, &firstActivation));
  REQUIRE_FALSE(firstActivation);
  REQUIRE_FALSE(KE_GFX_RefreshAdapter(display->adapter));
  KE_GFX_RemoveAdapter(display->adapter);
  REQUIRE_FALSE(kernel::gfx::active_video_present_target());
  REQUIRE(ungatedActualCallbacks == 0);
}

TEST_CASE("Closing an unused topology token releases its target reservation", "[gfx][topology]") {
  EnsureObjectManagerInitialized();
  const auto display = MakeFakeDisplay();
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  const auto targetMode = kernel::gfx::video_present_target_mode(
      topology.generation, display->videoPresentTargetId, 0);
  REQUIRE(targetMode);
  const auto sourceMode = MatchingSourceMode(topology.generation, targetMode->mode);
  REQUIRE(sourceMode);
  VideoPresentPath path{
      .sourceId = kernel::gfx::DesktopSourceId,
      .sourceModeId = sourceMode->modeId,
      .targetId = display->videoPresentTargetId,
      .targetModeId = targetMode->modeId,
      .width = targetMode->mode.width,
      .height = targetMode->mode.height};
  auto process =
      kernel::CreateKObject<kernel::KProcessObject>("closed-token-process").value();
  auto thread =
      kernel::CreateKObject<kernel::KThreadObject>("closed-token-thread").value();
  thread->owner_process = process.get();
  test_sched::SetCurrentThread(thread);

  __KRN_GFX_VALIDATEVIDEOPRESENTTOPOLOGY_V1 validate{};
  validate.generation = topology.generation;
  validate.paths = kernel::borrowed_ptr<VideoPresentPath>(&path, 1, {});
  validate.paths_len = 1;
  REQUIRE_FALSE(handle_ValidateVideoPresentTopology_V1(kstd::move(validate)));
  validate.paths.release();
  REQUIRE(g_fakeAdapterState.prepareCalls == 1);
  REQUIRE(g_fakeAdapterState.finishCalls == 0);

  SECTION("Closing the handle releases the reservation") {
    REQUIRE_FALSE(process->handles.Close(
        static_cast<kernel::KHandle>(validate.token)));
  }
  SECTION("Destroying the owning process releases the reservation") {
    test_sched::SetCurrentThread({});
    thread->owner_process = nullptr;
    process = nullptr;
  }
  REQUIRE(g_fakeAdapterState.finishCalls == 1);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
  test_sched::SetCurrentThread({});
}

TEST_CASE("Stale topology token is consumed without hardware programming", "[gfx][topology]") {
  EnsureObjectManagerInitialized();
  const auto display = MakeFakeDisplay();
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  const auto mode = kernel::gfx::video_present_target_mode(
      topology.generation, display->videoPresentTargetId, 0);
  REQUIRE(mode);
  const auto source = MatchingSourceMode(topology.generation, mode->mode);
  REQUIRE(source);
  VideoPresentPath path{
      .sourceId = kernel::gfx::DesktopSourceId, .sourceModeId = source->modeId,
      .targetId = display->videoPresentTargetId, .targetModeId = mode->modeId,
      .sourceX = 0, .sourceY = 0, .width = mode->mode.width,
      .height = mode->mode.height, .flags = 0};
  auto process = kernel::CreateKObject<kernel::KProcessObject>("stale-video-process").value();
  auto thread = kernel::CreateKObject<kernel::KThreadObject>("stale-video-thread").value();
  thread->owner_process = process.get();
  test_sched::SetCurrentThread(thread);
  __KRN_GFX_VALIDATEVIDEOPRESENTTOPOLOGY_V1 validate{};
  validate.generation = topology.generation;
  validate.paths = kernel::borrowed_ptr<VideoPresentPath>(&path, 1, {});
  validate.paths_len = 1;
  REQUIRE_FALSE(handle_ValidateVideoPresentTopology_V1(kstd::move(validate)));
  const auto token = validate.token;
  validate.paths.release();
  REQUIRE(process->handles.Lookup(static_cast<kernel::KHandle>(token)));

  SECTION("Adding a target invalidates the reservation") {
    REQUIRE(StartFakeAdapter().size() == 1);
  }
  SECTION("Resource notification invalidates the reservation") {
    REQUIRE_FALSE(KE_GFX_RefreshAdapter(display->adapter));
  }
  SECTION("Connection notification invalidates the reservation") {
    g_fakeAdapterState.firstHeadAbsent = true;
    REQUIRE_FALSE(KE_GFX_RefreshAdapter(display->adapter));
  }
  ResetFakeCalls();
  VideoPresentTargetResult result{};
  __KRN_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 commit{};
  commit.token = token;
  commit.results = kernel::borrowed_ptr<VideoPresentTargetResult>(&result, 1, {});
  commit.results_len = 1;
  REQUIRE(handle_CommitVideoPresentTopology_V1(kstd::move(commit)) ==
          kernel::KernelError::Retry);
  commit.results.release();
  REQUIRE_FALSE(display->surface);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
  REQUIRE(g_fakeAdapterState.finishCalls == 1);
  __KRN_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 consumed{};
  consumed.token = token;
  consumed.results = kernel::borrowed_ptr<VideoPresentTargetResult>(&result, 1, {});
  consumed.results_len = 1;
  REQUIRE(handle_CommitVideoPresentTopology_V1(kstd::move(consumed)) ==
          kernel::KernelError::InvalidHandle);
  consumed.results.release();
  test_sched::SetCurrentThread({});
}

TEST_CASE("Topology snapshots report the 64-record limit and stale generations retry", "[gfx][topology]") {
  EnsureObjectManagerInitialized();
  ResetFakeAdapterState();
  REQUIRE(StartFakeAdapter().size() == 1);
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.targetCount != 0);

  __KRN_GFX_GETVIDEOPRESENTTOPOLOGY_V1 query{};
  REQUIRE_FALSE(handle_GetVideoPresentTopology_V1(kstd::move(query)));
  REQUIRE(query.topology.recordLimit == kernel::gfx::VideoPresentRecordLimit);

  ResetFakeAdapterState();
  g_fakeAdapterState.headCount = 1;
  REQUIRE(StartFakeAdapter().size() == 1);
  REQUIRE(kernel::gfx::video_present_target(topology.generation, 0).error() ==
          kernel::KernelError::Retry);
}

TEST_CASE("Topology mode arrays accept 64 records and reject 65", "[gfx][topology]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  REQUIRE_FALSE(isolation.overflow);
  ResetFakeAdapterState();
  g_fakeAdapterState.modeCount = 64;
  for (size_t i = 0; i < g_fakeAdapterState.modeCount; ++i) {
    auto &mode = g_fakeAdapterState.modes[i];
    mode.width = 640 + static_cast<uint32_t>(i) * 8;
    mode.height = 480;
    mode.pitch = mode.width;
    mode.videoMemoryBitmapWidth = mode.width;
    mode.videoMemoryBitmapHeight = mode.height;
    mode.bpp = 8;
    mode.numberOfPlanes = 1;
    mode.bitsPerPlane = 8;
    mode.refreshRate = 60;
    mode.pixelFormat = kernel::PixelFormat::INDEX8;
  }
  auto displays = StartFakeAdapter();
  isolation.Track(displays);
  REQUIRE_FALSE(isolation.overflow);
  REQUIRE(displays.size() == 1);
  displays[0]->monitorCaps = kernel::KMonitorCapabilities{};

  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.targetModeCount == 64);
  REQUIRE(topology.sourceModeCount == 64);
  for (const size_t capacity : {size_t{0}, size_t{1}, size_t{63}, size_t{64}, size_t{65}}) {
    VideoPresentSourceMode sourceBuffer[65]{};
    sourceBuffer[0].modeId = 0xdeadbeef;
    sourceBuffer[64].modeId = 0xdeadbeef;
    __KRN_GFX_ENUMVIDEOPRESENTSOURCEMODES_V1 sourceBoundary{};
    sourceBoundary.generation = topology.generation;
    sourceBoundary.sourceId = kernel::gfx::DesktopSourceId;
    sourceBoundary.modes = kernel::borrowed_ptr<VideoPresentSourceMode>(
        sourceBuffer, capacity, {});
    const auto sourceError =
        handle_EnumVideoPresentSourceModes_V1(kstd::move(sourceBoundary));
    sourceBoundary.modes.release();
    VideoPresentTargetMode targetBuffer[65]{};
    targetBuffer[0].modeId = 0xdeadbeef;
    targetBuffer[64].modeId = 0xdeadbeef;
    __KRN_GFX_ENUMVIDEOPRESENTTARGETMODES_V1 targetBoundary{};
    targetBoundary.generation = topology.generation;
    targetBoundary.targetId = displays[0]->videoPresentTargetId;
    targetBoundary.modes = kernel::borrowed_ptr<VideoPresentTargetMode>(
        targetBuffer, capacity, {});
    const auto targetError =
        handle_EnumVideoPresentTargetModes_V1(kstd::move(targetBoundary));
    targetBoundary.modes.release();
    REQUIRE(sourceBoundary.required == 64);
    REQUIRE(targetBoundary.required == 64);
    if (capacity < 64) {
      REQUIRE(sourceError == kernel::KernelError::BufferTooSmall);
      REQUIRE(targetError == kernel::KernelError::BufferTooSmall);
      REQUIRE(sourceBoundary.written == 0);
      REQUIRE(targetBoundary.written == 0);
      REQUIRE(sourceBuffer[0].modeId == 0xdeadbeef);
      REQUIRE(targetBuffer[0].modeId == 0xdeadbeef);
    } else {
      REQUIRE_FALSE(sourceError);
      REQUIRE_FALSE(targetError);
      REQUIRE(sourceBoundary.written == 64);
      REQUIRE(targetBoundary.written == 64);
    }
    REQUIRE(sourceBuffer[64].modeId == 0xdeadbeef);
    REQUIRE(targetBuffer[64].modeId == 0xdeadbeef);
  }
  VideoPresentSourceMode sourceRecords[64]{};
  __KRN_GFX_ENUMVIDEOPRESENTSOURCEMODES_V1 sourceQuery{};
  sourceQuery.generation = topology.generation;
  sourceQuery.sourceId = kernel::gfx::DesktopSourceId;
  sourceQuery.queryOnly = 1;
  REQUIRE_FALSE(handle_EnumVideoPresentSourceModes_V1(kstd::move(sourceQuery)));
  REQUIRE(sourceQuery.required == 64);
  REQUIRE(sourceQuery.written == 0);
  sourceQuery.queryOnly = 0;
  sourceQuery.modes = kernel::borrowed_ptr<VideoPresentSourceMode>(sourceRecords, 64, {});
  REQUIRE_FALSE(handle_EnumVideoPresentSourceModes_V1(kstd::move(sourceQuery)));
  sourceQuery.modes.release();
  REQUIRE(sourceQuery.written == 64);
  VideoPresentTargetMode targetRecords[64]{};
  __KRN_GFX_ENUMVIDEOPRESENTTARGETMODES_V1 targetQuery{};
  targetQuery.generation = topology.generation;
  targetQuery.targetId = displays[0]->videoPresentTargetId;
  targetQuery.queryOnly = 1;
  REQUIRE_FALSE(handle_EnumVideoPresentTargetModes_V1(kstd::move(targetQuery)));
  REQUIRE(targetQuery.required == 64);
  REQUIRE(targetQuery.written == 0);
  targetQuery.queryOnly = 0;
  targetQuery.modes = kernel::borrowed_ptr<VideoPresentTargetMode>(targetRecords, 64, {});
  REQUIRE_FALSE(handle_EnumVideoPresentTargetModes_V1(kstd::move(targetQuery)));
  targetQuery.modes.release();
  REQUIRE(targetQuery.written == 64);
  const auto lastMode = kernel::gfx::video_present_target_mode(
      topology.generation, displays[0]->videoPresentTargetId, 63);
  REQUIRE(lastMode);
  REQUIRE(lastMode->mode.width == 640 + 63 * 8);

  g_fakeAdapterState.modeCount = 65;
  auto &extra = g_fakeAdapterState.modes[64];
  extra.width = 640 + 64 * 8;
  extra.height = 480;
  extra.pitch = extra.width;
  extra.bpp = 8;
  extra.numberOfPlanes = 1;
  extra.bitsPerPlane = 8;
  extra.refreshRate = 60;
  extra.pixelFormat = kernel::PixelFormat::INDEX8;
  REQUIRE(kernel::gfx::get_video_present_topology(topology) ==
          kernel::KernelError::ResourceLimitExceeded);
}

TEST_CASE("Target record arrays accept 64 records and reject 65", "[gfx][topology]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  REQUIRE_FALSE(isolation.overflow);
  ResetFakeAdapterState();
  size_t remaining = 64;
  while (remaining) {
    g_fakeAdapterState.headCount =
        remaining > 16 ? 16 : static_cast<size_t>(remaining);
    auto displays = StartFakeAdapter();
    isolation.Track(displays);
    REQUIRE_FALSE(isolation.overflow);
    REQUIRE(displays.size() == g_fakeAdapterState.headCount);
    remaining -= displays.size();
  }
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.targetCount == 64);
  VideoPresentTarget records[64]{};
  __KRN_GFX_ENUMVIDEOPRESENTTARGETS_V1 query{};
  query.generation = topology.generation;
  query.targets = kernel::borrowed_ptr<VideoPresentTarget>(records, 64, {});
  REQUIRE_FALSE(handle_EnumVideoPresentTargets_V1(kstd::move(query)));
  query.targets.release();
  REQUIRE(query.written == 64);

  g_fakeAdapterState.headCount = 1;
  auto extra = StartFakeAdapter();
  isolation.Track(extra);
  REQUIRE_FALSE(isolation.overflow);
  REQUIRE(extra.size() == 1);
  REQUIRE(kernel::gfx::get_video_present_topology(topology) ==
          kernel::KernelError::ResourceLimitExceeded);
}

TEST_CASE("One adapter is not silently capped at sixteen connectors", "[gfx][topology]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  ResetFakeAdapterState();
  SECTION("Seventeen connectors") { g_fakeAdapterState.headCount = 17; }
  SECTION("Sixty-four connectors") { g_fakeAdapterState.headCount = 64; }
  SECTION("Sixty-five connectors overflow the target record class") {
    g_fakeAdapterState.headCount = 65;
  }
  const auto displays = StartFakeAdapter();
  isolation.Track(displays);
  REQUIRE_FALSE(isolation.overflow);
  REQUIRE(displays.size() == g_fakeAdapterState.headCount);
  kernel::gfx::VideoPresentTopologyInfo topology{};
  const auto error = kernel::gfx::get_video_present_topology(topology);
  if (g_fakeAdapterState.headCount > 64) {
    REQUIRE(error == kernel::KernelError::ResourceLimitExceeded);
  } else {
    REQUIRE_FALSE(error);
    REQUIRE(topology.targetCount == g_fakeAdapterState.headCount);
    REQUIRE(topology.sourceModeCount == 1);
    REQUIRE(topology.targetModeCount == g_fakeAdapterState.headCount);
  }
}

TEST_CASE("Public enumeration handles empty classes without writes", "[gfx][topology]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.targetCount == 0);
  REQUIRE(topology.sourceModeCount == 0);
  __KRN_GFX_ENUMVIDEOPRESENTTARGETS_V1 targets{};
  targets.generation = topology.generation;
  targets.written = UINT32_MAX;
  REQUIRE_FALSE(handle_EnumVideoPresentTargets_V1(kstd::move(targets)));
  REQUIRE(targets.written == 0);
  __KRN_GFX_ENUMVIDEOPRESENTSOURCEMODES_V1 modes{};
  modes.generation = topology.generation;
  modes.sourceId = kernel::gfx::DesktopSourceId;
  modes.written = UINT32_MAX;
  REQUIRE_FALSE(handle_EnumVideoPresentSourceModes_V1(kstd::move(modes)));
  REQUIRE(modes.written == 0);
  __KRN_GFX_ENUMVIDEOPRESENTTARGETMODES_V1 stale{};
  stale.generation = topology.generation - 1;
  stale.targetId = UINT32_MAX;
  stale.written = UINT32_MAX;
  REQUIRE(handle_EnumVideoPresentTargetModes_V1(kstd::move(stale)) ==
          kernel::KernelError::Retry);
  REQUIRE(stale.written == 0);
}

TEST_CASE("Connection capabilities modes and resources invalidate old snapshot queries",
          "[gfx][topology][hotplug]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  isolation.Track(display);
  const auto checkMutation = [&](auto mutate) {
    kernel::gfx::VideoPresentTopologyInfo before{};
    REQUIRE_FALSE(kernel::gfx::get_video_present_topology(before));
    mutate();
    REQUIRE(kernel::gfx::video_present_target(before.generation, 0).error() ==
            kernel::KernelError::Retry);
    __KRN_GFX_ENUMVIDEOPRESENTTARGETS_V1 stale{};
    stale.generation = before.generation;
    REQUIRE(handle_EnumVideoPresentTargets_V1(kstd::move(stale)) ==
            kernel::KernelError::Retry);
    REQUIRE(stale.written == 0);
  };
  checkMutation([&] { display->isPresent = false; });
  checkMutation([&] { display->isPresent = true; });
  checkMutation([&] { display->monitorCaps.maxRefreshRate = 70; });
  checkMutation([&] { g_fakeAdapterState.modes[0].refreshRate = 65; });
  checkMutation([&] { ++display->adapter->resourceGeneration; });
}

TEST_CASE("Adapter refresh disconnects reconnects and PnP removal retires targets",
          "[gfx][topology][hotplug]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  isolation.Track(display);
  const auto identity = display->videoPresentTargetId;
  REQUIRE(SetModeViaTopology(display, 0));
  kernel::gfx::VideoPresentTopologyInfo before{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(before));
  g_fakeAdapterState.firstHeadAbsent = true;
  REQUIRE_FALSE(KE_GFX_RefreshAdapter(display->adapter));
  REQUIRE_FALSE(display->isPresent);
  REQUIRE(display->videoPresentTargetId == identity);
  REQUIRE_FALSE(kernel::gfx::active_video_present_target());
  REQUIRE(kernel::gfx::video_present_target(before.generation, 0).error() ==
          kernel::KernelError::Retry);
  g_fakeAdapterState.firstHeadAbsent = false;
  REQUIRE_FALSE(KE_GFX_RefreshAdapter(display->adapter));
  REQUIRE(display->isPresent);
  REQUIRE(display->videoPresentTargetId == identity);
  REQUIRE(SetModeViaTopology(display, 0));
  REQUIRE_FALSE(KE_PNP_ReportRemovedDevice(display->adapter->device));
  while (kernel::event::detail::pump_once()) {}
  kernel::gfx::VideoPresentTopologyInfo after{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(after));
  REQUIRE(after.targetCount == 0);
  REQUIRE(after.pathCount == 0);
  REQUIRE(display->removed);
  REQUIRE_FALSE(kernel::gfx::active_video_present_target());
}

TEST_CASE("PnP removal waits for painting before retiring state or invalidating I/O",
          "[gfx][topology][hotplug][removal]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  static kernel::KObjectPtr<kernel::KDisplay> painting;
  painting = MakeFakeDisplay();
  isolation.Track(painting);
  REQUIRE(SetModeViaTopology(painting, 0));
  painting->adapter->Present = [](
      const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
      const kernel::KObjectPtr<kernel::KSurface> &, const kernel::KClipRect *, size_t) {
    ++g_fakeAdapterState.presentCalls;
    if (adapter->device->pnpState == kernel::KPnpState::Removed)
      ++g_fakeAdapterState.removedCallbacks;
  };
  static bool yielded, remainedActive, deviceStillUsable;
  yielded = remainedActive = deviceStillUsable = false;
  kernel::gfx::VideoPresentTopologyInfo before{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(before));
  REQUIRE_FALSE(kernel::event::broadcast(
      painting->adapter->device, kernel::event::make_event_code(
          KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Started)));
  kernel::user::lock();
  kernel::user::begin_scene_draw_locked();
  kernel::user::present_locked(kernel::user::URect{0, 0, 8, 8});
  test_sched::SetYieldHook([] {
    yielded = true;
    // The removal call is blocked at the scene lock. Run the concurrent query
    // and finish the in-flight paint before allowing that lock acquisition.
    kernel::gfx::VideoPresentTopologyInfo during{};
    remainedActive = !kernel::gfx::get_video_present_topology(during) &&
                     during.pathCount == 1 &&
                     kernel::gfx::active_video_present_target() == painting;
    deviceStillUsable = painting->adapter->device->pnpState != kernel::KPnpState::Removed;
    kernel::user::end_scene_draw_locked();
    kernel::user::unlock();
  });
  const auto error = KE_PNP_ReportRemovedDevice(painting->adapter->device);
  test_sched::SetYieldHook(nullptr);
  // Keep the failure path bounded if removal ever stops acquiring scene lock.
  if (!yielded) {
    kernel::user::end_scene_draw_locked();
    kernel::user::unlock();
  }
  REQUIRE_FALSE(error);
  REQUIRE(yielded);
  REQUIRE(remainedActive);
  REQUIRE(deviceStillUsable);
  REQUIRE(g_fakeAdapterState.presentCalls == 1);
  REQUIRE(g_fakeAdapterState.removedCallbacks == 0);
  REQUIRE(painting->removed);
  REQUIRE(painting->adapter->device->pnpState == kernel::KPnpState::Removed);
  REQUIRE_FALSE(kernel::gfx::active_video_present_target());
  kernel::gfx::VideoPresentTopologyInfo after{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(after));
  REQUIRE(after.pathCount == 0);
  REQUIRE(after.targetCount == 0);
  REQUIRE(after.generation != before.generation);
  {
    kernel::user::Guard scene;
    kernel::user::present_locked(kernel::user::URect{0, 0, 8, 8});
  }
  while (kernel::event::detail::pump_once()) {}
  REQUIRE(g_fakeAdapterState.presentCalls == 1);
  REQUIRE(g_fakeAdapterState.removedCallbacks == 0);
  painting = nullptr;
}

TEST_CASE("Adapter refresh discovers new heads and retires an unsupported active mode",
          "[gfx][topology][hotplug]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  isolation.Track(display);
  REQUIRE(SetModeViaTopology(display, 0));
  g_fakeAdapterState.modeCount = 0;
  REQUIRE_FALSE(KE_GFX_RefreshAdapter(display->adapter));
  REQUIRE_FALSE(display->isAttachedToDesktop);
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.pathCount == 0);
  g_fakeAdapterState.modeCount = 1;
  g_fakeAdapterState.headCount = 2;
  REQUIRE_FALSE(KE_GFX_RefreshAdapter(display->adapter));
  const auto heads = DisplaysOf(display->adapter);
  REQUIRE(heads.size() == 2);
  for (const auto &head : heads)
    if (head != display) isolation.Track(head);
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.targetCount == 2);
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

TEST_CASE("Init confirms an existing path instead of trusting the summary count",
          "[gfx][topology][init]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  isolation.Track(display);
  const auto activated = SetModeViaTopology(display, 0);
  REQUIRE(activated);
  REQUIRE(activated->outcome == 0);
  bool expected = false, changeGeneration = false;
  SECTION("Confirmed active path") { expected = true; }
  SECTION("Unknown actual hardware state") { display->modeGeneration = 0; }
  SECTION("Disabled actual hardware state") { display->isAttachedToDesktop = false; }
  SECTION("Degraded topology with an enumerable path") { display->topologyDegraded = true; }
  SECTION("Generation changes between summary and path query") { changeGeneration = true; }
  __KRN_GFX_GETVIDEOPRESENTTOPOLOGY_V1 summary{};
  REQUIRE_FALSE(handle_GetVideoPresentTopology_V1(kstd::move(summary)));
  REQUIRE(summary.topology.pathCount == 1);
  bool enumerated = false;
  uint32_t queryGeneration = 0, queryWritten = 0;
  kstd::error_code queryError{};
  const bool confirmed = init::confirm_existing_display_path(summary.topology,
      [&](uint32_t generation, uint32_t firstIndex, uint32_t queryOnly, VideoPresentPath *path,
          uint32_t *capacity, uint32_t *required, uint32_t *written) {
        enumerated = true;
        queryGeneration = generation;
        if (changeGeneration) ++display->adapter->resourceGeneration;
        __KRN_GFX_ENUMVIDEOPRESENTPATHS_V1 query{};
        query.generation = generation;
        query.firstIndex = firstIndex;
        query.queryOnly = queryOnly;
        query.paths = kernel::borrowed_ptr<VideoPresentPath>(path, *capacity, {});
        queryError = handle_EnumVideoPresentPaths_V1(kstd::move(query));
        query.paths.release();
        *written = queryWritten = query.written;
        *required = query.required;
        return kernel::KErrorToSysReturnCode(queryError);
      });
  const bool invalidActual = !display->modeGeneration || !display->isAttachedToDesktop;
  display->modeGeneration = 1;
  display->isAttachedToDesktop = true;
  display->topologyDegraded = false;
  REQUIRE(enumerated);
  REQUIRE(queryGeneration == summary.topology.generation);
  REQUIRE(confirmed == expected);
  if (changeGeneration) {
    REQUIRE(queryError == kernel::KernelError::Retry);
    REQUIRE(queryWritten == 0);
  } else if (invalidActual) {
    REQUIRE(queryError == kernel::KernelError::InvalidState);
    REQUIRE(summary.topology.degraded == 1);
    REQUIRE(queryWritten == 0);
  } else {
    REQUIRE_FALSE(queryError);
    REQUIRE(queryWritten == 1);
  }
}

  TEST_CASE("Generated topology enumeration supports query fill and error counts without partial writes",
            "[gfx][topology][dispatch][snapshot]") {
    EnsureObjectManagerInitialized();
    auto process = kernel::CreateAnonymousKObject<kernel::KProcessObject>().value();
    auto thread = kernel::CreateAnonymousKObject<kernel::KThreadObject>().value();
    thread->owner_process = process.get();
    test_sched::SetCurrentThread(thread);
    struct ResetThread { ~ResetThread() { test_sched::SetCurrentThread({}); } } resetThread;
    TargetIdIsolation isolation;
    const auto display = MakeFakeDisplay();
    isolation.Track(display);
    kernel::gfx::VideoPresentTopologyInfo topology{};
    REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
    __SYS_GFX_ENUMVIDEOPRESENTTARGETS_V1 args{};
    args.generation = topology.generation;
    args.queryOnly = 1;
    dispatchEmptyMaps = 0;
    REQUIRE_FALSE(Dispatch_EnumVideoPresentTargets_V1(&args, sizeof(args), 0));
    REQUIRE(args.required == 1);
    REQUIRE(args.written == 0);
    VideoPresentTarget records[2]{};
    records[0].targetId = records[1].targetId = UINT32_MAX;
    for (auto *pointer : {static_cast<VideoPresentTarget *>(nullptr), records}) {
      args.targets = pointer;
      args.targetsLength = 0;
      args.queryOnly = 0;
      args.required = args.written = UINT32_MAX;
      REQUIRE(kernel::KErrorToSysReturnCode(
          Dispatch_EnumVideoPresentTargets_V1(&args, sizeof(args), 0)) ==
          SyscallReturnCode_INVALID_SIZE);
      REQUIRE(args.required == 1);
      REQUIRE(args.written == 0);
      REQUIRE(records[0].targetId == UINT32_MAX);
    }
    REQUIRE(dispatchEmptyMaps == 0);
    args.targets = records;
    args.targetsLength = 2;
    args.queryOnly = 2;
    REQUIRE(Dispatch_EnumVideoPresentTargets_V1(&args, sizeof(args), 0) ==
            kernel::KernelError::InvalidArgument);
    REQUIRE(args.required == 0);
    REQUIRE(args.written == 0);
    args.queryOnly = 1;
    REQUIRE(Dispatch_EnumVideoPresentTargets_V1(&args, sizeof(args), 0) ==
            kernel::KernelError::InvalidArgument);
    args.queryOnly = 0;
    REQUIRE_FALSE(Dispatch_EnumVideoPresentTargets_V1(&args, sizeof(args), 0));
    REQUIRE(args.required == 1);
    REQUIRE(args.written == 1);
    REQUIRE(records[0].targetId == display->videoPresentTargetId);
    REQUIRE(records[1].targetId == UINT32_MAX);
    args.firstIndex = 1;
    args.targets = nullptr;
    args.targetsLength = 0;
    REQUIRE_FALSE(Dispatch_EnumVideoPresentTargets_V1(&args, sizeof(args), 0));
    REQUIRE(args.required == 0);
    REQUIRE(args.written == 0);
    args.firstIndex = 2;
    REQUIRE(Dispatch_EnumVideoPresentTargets_V1(&args, sizeof(args), 0) ==
            kernel::KernelError::InvalidArgument);
    args.firstIndex = 0;
    ++display->adapter->resourceGeneration;
    REQUIRE(Dispatch_EnumVideoPresentTargets_V1(&args, sizeof(args), 0) ==
            kernel::KernelError::Retry);
    REQUIRE(args.required == 0);
    REQUIRE(args.written == 0);
  }

TEST_CASE("Generated enumeration preserves null output capacity for every record class",
          "[gfx][topology][dispatch][snapshot]") {
  EnsureObjectManagerInitialized();
  auto process = kernel::CreateAnonymousKObject<kernel::KProcessObject>().value();
  auto thread = kernel::CreateAnonymousKObject<kernel::KThreadObject>().value();
  thread->owner_process = process.get();
  test_sched::SetCurrentThread(thread);
  struct ResetThread { ~ResetThread() { test_sched::SetCurrentThread({}); } } resetThread;
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  isolation.Track(display);
  g_fakeAdapterState.modeCount = 2;
  REQUIRE(SetModeViaTopology(display, 0));
  const auto snapshot = kernel::gfx::acquire_video_present_snapshot();
  REQUIRE(snapshot);
  const auto generation = (*snapshot)->topology.generation;
  const auto check = []<typename Args, typename Record>(
      Args args, Record *, auto dispatch, auto setBuffer) {
    Record records[3];
    ::memset(records, 0xa5, sizeof(records));
    args.queryOnly = 1;
    setBuffer(args, static_cast<Record *>(nullptr), 0);
    REQUIRE_FALSE(dispatch(&args, sizeof(args), 0));
    const auto required = args.required;
    REQUIRE(required > 0);
    REQUIRE(required < 3);
    REQUIRE(args.written == 0);
    for (auto *pointer : {static_cast<Record *>(nullptr), records}) {
      setBuffer(args, pointer, 1);
      args.required = args.written = UINT32_MAX;
      REQUIRE(dispatch(&args, sizeof(args), 0) == kernel::KernelError::InvalidArgument);
      REQUIRE(args.required == 0);
      REQUIRE(args.written == 0);
    }
    setBuffer(args, records, 0);
    REQUIRE_FALSE(dispatch(&args, sizeof(args), 0));
    REQUIRE(args.required == required);
    REQUIRE(args.written == 0);
    args.queryOnly = 0;
    setBuffer(args, records, 3);
    dispatchInvalidAddress = records;
    const auto invalidMapping = dispatch(&args, sizeof(args), 0);
    dispatchInvalidAddress = nullptr;
    REQUIRE(invalidMapping == kernel::KernelError::InvalidAddress);
    REQUIRE_FALSE(dispatch(&args, sizeof(args), 0));
    REQUIRE(args.required == required);
    REQUIRE(args.written == required);
    const auto *tail = reinterpret_cast<const uint8_t *>(records + required);
    for (size_t i = 0; i < (3 - required) * sizeof(Record); ++i)
      REQUIRE(tail[i] == 0xa5);
  };
  __SYS_GFX_ENUMVIDEOPRESENTSOURCEMODES_V1 sources{};
  sources.generation = generation;
  sources.sourceId = kernel::gfx::DesktopSourceId;
  check(sources, static_cast<VideoPresentSourceMode *>(nullptr),
      Dispatch_EnumVideoPresentSourceModes_V1,
      [](auto &args, auto *pointer, uint32_t count) { args.modes = pointer; args.modesLength = count; });
  __SYS_GFX_ENUMVIDEOPRESENTTARGETS_V1 targets{};
  targets.generation = generation;
  check(targets, static_cast<VideoPresentTarget *>(nullptr),
      Dispatch_EnumVideoPresentTargets_V1,
      [](auto &args, auto *pointer, uint32_t count) { args.targets = pointer; args.targetsLength = count; });
  __SYS_GFX_ENUMVIDEOPRESENTTARGETMODES_V1 modes{};
  modes.generation = generation;
  modes.targetId = display->videoPresentTargetId;
  check(modes, static_cast<VideoPresentTargetMode *>(nullptr),
      Dispatch_EnumVideoPresentTargetModes_V1,
      [](auto &args, auto *pointer, uint32_t count) { args.modes = pointer; args.modesLength = count; });
  __SYS_GFX_ENUMVIDEOPRESENTPATHS_V1 paths{};
  paths.generation = generation;
  check(paths, static_cast<VideoPresentPath *>(nullptr),
      Dispatch_EnumVideoPresentPaths_V1,
      [](auto &args, auto *pointer, uint32_t count) { args.paths = pointer; args.pathsLength = count; });
}

TEST_CASE("Mode observation and refresh run outside real spinlocks",
          "[gfx][topology][snapshot][hotplug]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  isolation.Track(display);
  kernel::gfx::video_present_test_set_spinlock_observer(
      [] { return test_sync::held_spinlocks() != 0; });
  struct ResetObserver {
    ~ResetObserver() { kernel::gfx::video_present_test_set_spinlock_observer(nullptr); }
  } resetObserver;
  display->GetModeInfo = ObserveModeCallback;
  observedModeCallbacks = modeCallbacksWithSpinlocks = 0;
  REQUIRE(test_sync::held_spinlocks() == 0);
  REQUIRE(kernel::gfx::video_present_test_locked_allocations() == 0);
  REQUIRE(kernel::gfx::acquire_video_present_snapshot());
  REQUIRE(SetModeViaTopology(display, 0));
  REQUIRE_FALSE(KE_GFX_RefreshAdapter(display->adapter));
  REQUIRE(kernel::gfx::acquire_video_present_snapshot());
  REQUIRE(observedModeCallbacks > 0);
  REQUIRE(modeCallbacksWithSpinlocks == 0);
  REQUIRE(test_sync::held_spinlocks() == 0);
  REQUIRE(kernel::gfx::video_present_test_locked_allocations() == 0);
  display->GetModeInfo = FakeGetModeInfo;
}

TEST_CASE("Mapped enumeration pins published storage or returns retry without rebuilding",
          "[gfx][topology][dispatch][snapshot]") {
  EnsureObjectManagerInitialized();
  auto process = kernel::CreateAnonymousKObject<kernel::KProcessObject>().value();
  auto thread = kernel::CreateAnonymousKObject<kernel::KThreadObject>().value();
  thread->owner_process = process.get();
  test_sched::SetCurrentThread(thread);
  struct ResetThread { ~ResetThread() { test_sched::SetCurrentThread({}); } } resetThread;
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  isolation.Track(display);
  const auto old = kernel::gfx::acquire_video_present_snapshot();
  REQUIRE(old);
  display->GetModeInfo = ObserveModeCallback;
  REQUIRE_FALSE(KE_GFX_RefreshAdapter(display->adapter));
  __SYS_GFX_ENUMVIDEOPRESENTSOURCEMODES_V1 args{};
  args.generation = kernel::gfx::video_present_test_generation();
  REQUIRE(args.generation != (*old)->topology.generation);
  args.sourceId = kernel::gfx::DesktopSourceId;
  VideoPresentSourceMode output{};
  output.modeId = UINT32_MAX;
  args.modes = &output;
  args.modesLength = 1;
  dispatchObservedOutput = &output;
  observedModeCallbacks = modeCallbacksWithOutputMapping = 0;
  const auto allocations = kernel::gfx::video_present_test_live_allocations();
  REQUIRE(Dispatch_EnumVideoPresentSourceModes_V1(&args, sizeof(args), 0) ==
          kernel::KernelError::Retry);
  REQUIRE(args.required == 0);
  REQUIRE(args.written == 0);
  REQUIRE(output.modeId == UINT32_MAX);
  REQUIRE(observedModeCallbacks == 0);
  REQUIRE(kernel::gfx::video_present_test_live_allocations() == allocations);
  // An unmapped size query may build the dirty generation.
  args.queryOnly = 1;
  args.modes = nullptr;
  args.modesLength = 0;
  REQUIRE_FALSE(Dispatch_EnumVideoPresentSourceModes_V1(&args, sizeof(args), 0));
  REQUIRE(args.required == 1);
  REQUIRE(observedModeCallbacks > 0);
  const auto publishedAllocations = kernel::gfx::video_present_test_live_allocations();
  observedModeCallbacks = 0;
  args.queryOnly = 0;
  args.modes = &output;
  args.modesLength = 1;
  REQUIRE_FALSE(Dispatch_EnumVideoPresentSourceModes_V1(&args, sizeof(args), 0));
  REQUIRE(args.written == 1);
  REQUIRE(output.modeId != UINT32_MAX);
  REQUIRE(observedModeCallbacks == 0);
  REQUIRE(modeCallbacksWithOutputMapping == 0);
  REQUIRE(kernel::gfx::video_present_test_live_allocations() == publishedAllocations);
  dispatchObservedOutput = nullptr;
  display->GetModeInfo = FakeGetModeInfo;
}

  TEST_CASE("Topology snapshot leases pin immutable records across replacement and removal",
            "[gfx][topology][snapshot]") {
    EnsureObjectManagerInitialized();
    TargetIdIsolation isolation;
    const auto display = MakeFakeDisplay();
    isolation.Track(display);
    const auto old = kernel::gfx::acquire_video_present_snapshot();
    REQUIRE(old);
    const auto generation = (*old)->topology.generation;
    const auto oldWidth = (*old)->targetModes[0].mode.width;
    const auto id = (*old)->targets[0].targetId;
    REQUIRE((*old)->targetReferences[0] == display);
    REQUIRE((*old)->allocatedBytes <= kernel::gfx::VideoPresentSnapshotBudgetBytes);
    g_fakeAdapterState.modes[0].width += 8;
    const auto replacement = kernel::gfx::acquire_video_present_snapshot();
    REQUIRE(replacement);
    REQUIRE((*replacement)->topology.generation != generation);
    REQUIRE((*replacement)->targetModes[0].mode.width == oldWidth + 8);
    REQUIRE((*old)->targetModes[0].mode.width == oldWidth);
    REQUIRE((*old)->targets[0].targetId == id);
    REQUIRE(kernel::gfx::acquire_video_present_snapshot(generation).error() ==
            kernel::KernelError::Retry);
    KE_GFX_RemoveAdapter(display->adapter);
    ResetFakeCalls();
    const auto removed = kernel::gfx::acquire_video_present_snapshot();
    REQUIRE(removed);
    REQUIRE((*removed)->topology.targetCount == 0);
    REQUIRE((*old)->targetReferences[0] == display);
    REQUIRE((*old)->targetModes[0].mode.width == oldWidth);
    REQUIRE(g_fakeAdapterState.removedCallbacks == 0);
  }

  TEST_CASE("Snapshot allocation failures free partial builders without publishing or locked reclamation",
            "[gfx][topology][snapshot]") {
    EnsureObjectManagerInitialized();
    TargetIdIsolation isolation;
    const auto display = MakeFakeDisplay();
    isolation.Track(display);
    const auto old = kernel::gfx::acquire_video_present_snapshot();
    REQUIRE(old);
    const auto live = kernel::gfx::video_present_test_live_allocations();
    const auto generation = (*old)->topology.generation;
    ++display->adapter->resourceGeneration;
    for (size_t failure = 0; failure < 7; ++failure) {
      kernel::gfx::video_present_test_fail_allocation_after(failure);
      const auto attempt = kernel::gfx::acquire_video_present_snapshot();
      kernel::gfx::video_present_test_fail_allocation_after(SIZE_MAX);
      REQUIRE_FALSE(attempt);
      REQUIRE(attempt.error() == kernel::KernelError::OutOfMemory);
      REQUIRE(kernel::gfx::video_present_test_live_allocations() == live);
      REQUIRE((*old)->topology.generation == generation);
      REQUIRE((*old)->targetReferences[0] == display);
    }
    const auto current = kernel::gfx::acquire_video_present_snapshot();
    REQUIRE(current);
    REQUIRE((*current)->topology.generation != generation);
    REQUIRE(kernel::gfx::video_present_test_locked_allocations() == 0);
  }

  TEST_CASE("Snapshot construction discards resource changes between counting and filling",
            "[gfx][topology][snapshot]") {
    EnsureObjectManagerInitialized();
    TargetIdIsolation isolation;
    const auto display = MakeFakeDisplay();
    isolation.Track(display);
    // One-mode count pass uses three callbacks; change during its fill pass.
    g_fakeAdapterState.modeCallsUntilResourceChange = 4;
    const auto changed = kernel::gfx::acquire_video_present_snapshot();
    REQUIRE_FALSE(changed);
    REQUIRE(changed.error() == kernel::KernelError::Retry);
    REQUIRE(kernel::gfx::acquire_video_present_snapshot());
  }

  TEST_CASE("Commit snapshot allocation failure preserves the token and reservation before hardware",
            "[gfx][topology][snapshot]") {
    EnsureObjectManagerInitialized();
    TargetIdIsolation isolation;
    const auto display = MakeFakeDisplay();
    isolation.Track(display);
    const auto snapshot = kernel::gfx::acquire_video_present_snapshot();
    REQUIRE(snapshot);
    const auto &mode = (*snapshot)->targetModes[0];
    VideoPresentPath path{.sourceId = kernel::gfx::DesktopSourceId, .sourceModeId = 1,
        .targetId = mode.targetId, .targetModeId = mode.modeId,
        .width = mode.mode.width, .height = mode.mode.height};
    auto process = kernel::CreateAnonymousKObject<kernel::KProcessObject>().value();
    auto thread = kernel::CreateAnonymousKObject<kernel::KThreadObject>().value();
    thread->owner_process = process.get();
    test_sched::SetCurrentThread(thread);
    __KRN_GFX_VALIDATEVIDEOPRESENTTOPOLOGY_V1 validate{};
    validate.generation = (*snapshot)->topology.generation;
    validate.paths = kernel::borrowed_ptr<VideoPresentPath>(&path, 1, {});
    validate.paths_len = 1;
    const auto validation = handle_ValidateVideoPresentTopology_V1(kstd::move(validate));
    validate.paths.release();
    REQUIRE_FALSE(validation);
    ResetFakeCalls();
    VideoPresentTargetResult result{};
    __KRN_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 commit{};
    commit.token = validate.token;
    commit.results = kernel::borrowed_ptr<VideoPresentTargetResult>(&result, 1, {});
    commit.results_len = 1;
    const auto live = kernel::gfx::video_present_test_live_allocations();
    for (size_t failure = 0; failure < 8; ++failure) {
      commit.results = kernel::borrowed_ptr<VideoPresentTargetResult>(&result, 1, {});
      kernel::gfx::video_present_test_fail_allocation_after(failure);
      const auto error = handle_CommitVideoPresentTopology_V1(kstd::move(commit));
      kernel::gfx::video_present_test_fail_allocation_after(SIZE_MAX);
      commit.results.release();
      REQUIRE(error == kernel::KernelError::OutOfMemory);
      REQUIRE(commit.required == 1);
      REQUIRE(commit.written == 0);
      REQUIRE(process->handles.Lookup(static_cast<kernel::KHandle>(validate.token)));
      REQUIRE(g_fakeAdapterState.finishCalls == 0);
      REQUIRE(CallLog().empty());
      REQUIRE(kernel::gfx::video_present_test_live_allocations() == live);
    }
    REQUIRE_FALSE(process->handles.Close(static_cast<kernel::KHandle>(validate.token)));
    REQUIRE(g_fakeAdapterState.finishCalls == 1);
    test_sched::SetCurrentThread({});
  }

TEST_CASE("Snapshot final release reclaims replaced arrays outside topology serialization",
          "[gfx][topology][snapshot]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  isolation.Track(display);
  {
    const auto initial = kernel::gfx::acquire_video_present_snapshot();
    REQUIRE(initial);
  }
  const auto baseline = kernel::gfx::video_present_test_live_allocations();
  for (size_t replacement = 0; replacement < 4; ++replacement) {
    {
      const auto prior = kernel::gfx::acquire_video_present_snapshot();
      REQUIRE(prior);
      ++display->adapter->resourceGeneration;
      const auto next = kernel::gfx::acquire_video_present_snapshot();
      REQUIRE(next);
      REQUIRE((*prior)->topology.generation != (*next)->topology.generation);
      REQUIRE(kernel::gfx::video_present_test_live_allocations() > baseline);
    }
    REQUIRE(kernel::gfx::video_present_test_live_allocations() == baseline);
  }
  REQUIRE(kernel::gfx::video_present_test_locked_allocations() == 0);
}

TEST_CASE("Boot refresh zero does not select an offered non-60-Hz timing", "[gfx][mode]") {
  init::DisplayPreference preference{640, 480, 8, 0};
  REQUIRE_FALSE(init::matches_display_preference(preference, 640, 480, 8, 75));
  REQUIRE(init::matches_display_preference(preference, 640, 480, 8, 60));
  preference.refreshHz = 75;
  REQUIRE(init::matches_display_preference(preference, 640, 480, 8, 75));
  REQUIRE_FALSE(init::matches_display_preference(preference, 640, 480, 8, 60));
}

TEST_CASE("Topology commit reuses the same KSurface across a mode switch", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.modeCount = 2;

  const auto modeResult = SetModeViaTopology(display, 0);
  const auto surface = display->surface;
  REQUIRE(modeResult);
  REQUIRE(surface);
  REQUIRE(surface->width == 640);
  REQUIRE(display->currentMode.width == 640);

  REQUIRE(SetModeViaTopology(display, 1));

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

TEST_CASE("Topology commit disables before programming and enables after", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.modeCount = 2;
  ResetFakeCalls();

  REQUIRE(SetModeViaTopology(display, 1));
  REQUIRE((CallLog() == "PDSEF" || CallLog() == "PDSEFD"));
}

TEST_CASE("Topology commit rejects an out-of-range mode without programming", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();

  REQUIRE(SetModeViaTopology(display, 7).error() == kernel::KernelError::NotFound);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);
}

TEST_CASE("A failing driver SetMode reports failure without fabricating a mode", "[gfx][mode]") {
  EnsureObjectManagerInitialized();

  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.modeCount = 2;

  REQUIRE(SetModeViaTopology(display, 0));
  const auto surface = display->surface;
  REQUIRE(surface);

  g_fakeAdapterState.setModeShouldSucceed = false;
  ResetFakeCalls();

  const auto failedMode = SetModeViaTopology(display, 1);
  REQUIRE(failedMode);
  REQUIRE(failedMode->outcome == VideoPresentOutcome_ProgrammingFailed);

  // Previous mode stays programmed, and the output is brought back up.
  REQUIRE(display->surface == surface);
  REQUIRE(surface->width == 640);
  REQUIRE(display->currentMode.width == 640);
  REQUIRE(CallLog() == "PDSEF");
  REQUIRE(kernel::gfx::active_video_present_target() == display);
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.pathCount == 1);
  const auto activePath = kernel::gfx::video_present_path(topology.generation, 0);
  REQUIRE(activePath);
  REQUIRE(activePath->targetModeId == 1);
}

TEST_CASE("A failed target enable restores and reports the previous active path", "[gfx][mode]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  g_fakeAdapterState.modeCount = 2;
  REQUIRE(SetModeViaTopology(display, 0));
  const auto surface = display->surface;
  REQUIRE(surface);

  ResetFakeCalls();
  g_fakeAdapterState.failEnableCallsRemaining = 1;
  const auto result = SetModeViaTopology(display, 1);
  REQUIRE(result);
  REQUIRE(result->outcome == VideoPresentOutcome_EnableFailed);
  REQUIRE(result->degraded == 0);
  REQUIRE(result->active == 1);
  REQUIRE(result->actualModeId == 1);
  REQUIRE(display->surface == surface);
  REQUIRE(display->currentMode.modeIndex == 0);
  REQUIRE(CallLog() == "PDSEEF");

  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.pathCount == 1);
  REQUIRE(topology.degraded == 0);
  const auto activePath = kernel::gfx::video_present_path(topology.generation, 0);
  REQUIRE(activePath);
  REQUIRE(activePath->targetModeId == 1);
}

TEST_CASE("A target that cannot restore reports its actual degraded state", "[gfx][mode]") {
  EnsureObjectManagerInitialized();
  TargetIdIsolation isolation;
  const auto display = MakeFakeDisplay();
  isolation.Track(display);
  g_fakeAdapterState.modeCount = 2;
  REQUIRE(SetModeViaTopology(display, 0));
  const auto oldSurface = display->surface;
  REQUIRE(oldSurface);

  g_fakeAdapterState.setModeShouldSucceed = false;
  g_fakeAdapterState.programFailureChangesMode = true;
  g_fakeAdapterState.bindRestoreMode = false;
  display->RestoreMode = nullptr;
  const auto result = SetModeViaTopology(display, 1);
  REQUIRE(result);
  REQUIRE(result->outcome == VideoPresentOutcome_StateUnknown);
  REQUIRE(result->degraded == 1);
  REQUIRE(result->active == 0);
  REQUIRE(result->actualModeId == 2);
  REQUIRE(display->surface == oldSurface);
  REQUIRE(display->currentMode.modeIndex == 1);
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  REQUIRE(topology.pathCount == 0);
  REQUIRE(topology.degraded == 1);
  display->topologyDegraded = false;
}

// ---------------------------------------------------------------------------
// Monitor capabilities
// ---------------------------------------------------------------------------

TEST_CASE("KMonitorCapabilities::Supports filters against the timing list", "[gfx][monitor]") {
  kernel::KMonitorCapabilities caps{};
  REQUIRE_FALSE(caps.Add(kernel::KMonitorTiming{.width = 640, .height = 480, .refreshRate = 60}));
  REQUIRE_FALSE(caps.Add(kernel::KMonitorTiming{.width = 800, .height = 600, .refreshRate = 0}));

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
  REQUIRE(SetModeViaTopology(display, 1).error() == kernel::KernelError::NotFound);
  REQUIRE(g_fakeAdapterState.setModeCalls == 0);

  // The mode the default monitor accepts still programs.
  REQUIRE(SetModeViaTopology(display, 0));
}
