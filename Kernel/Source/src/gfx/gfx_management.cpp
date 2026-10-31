
#include "stdkrn.hpp"

#include "gfx/gfx.hpp"

#include <kernel/mutex.hpp>
#include "user/user.hpp"

namespace kernel::gfx {
void refresh_observed_topology() noexcept;
}

namespace {
kernel::Mutex g_topology_lock;
kernel::KDispatcherHeader g_driver_available{.signalled = true, .manualReset = false};
uint32_t g_topology_generation = 1;
uint32_t g_next_target_id = 1;
kernel::KObjectPtr<kernel::gfx::KVideoPresentSource> g_desktop_source;
kernel::gfx::VideoPresentPathInfo g_active_path{};
bool g_active_path_valid = false;
kernel::gfx::VideoPresentSnapshot *g_snapshot{};
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
size_t g_snapshot_allocations_until_failure = SIZE_MAX;
size_t g_snapshot_live_allocations{}, g_snapshot_locked_allocations{};
size_t g_topology_depth{};
bool (*g_snapshot_spinlock_observer)(){};
void (*g_before_commit_decision)(){};
size_t g_callback_gate_depth{};
#endif

struct TopologyLock {
  TopologyLock() {
    g_topology_lock.lock();
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
    ++g_topology_depth;
#endif
  }
  ~TopologyLock() {
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
    --g_topology_depth;
#endif
    g_topology_lock.unlock();
  }
};

struct DriverLock {
  DriverLock() {
    if (KE_SCHED_WaitForHeader(g_driver_available, kernel::WAIT_INFINITE))
      HAL_PLATFORM_Panic("graphics callback gate requires schedulable context", __FILE__, __LINE__);
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
    ++g_callback_gate_depth;
#endif
  }
  ~DriverLock() {
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
    --g_callback_gate_depth;
#endif
    KE_EVENT_Signal(g_driver_available, false);
  }
};

struct AdapterHeadScratch {
  kernel::KGraphicsAdapterDisplayInfo info{};
  kernel::Edid edid{};
  void Reset() {
    info.~KGraphicsAdapterDisplayInfo();
    new (&info) kernel::KGraphicsAdapterDisplayInfo{};
    edid.~Edid();
    new (&edid) kernel::Edid{};
  }
};

struct RefreshRecord {
  kernel::KObjectPtr<kernel::KDisplay> target;
  kernel::Edid edid{};
  kernel::KMonitorCapabilities capabilities{};
  bool valid{}, present{}, monitor{}, currentModeSupported{};
};

struct RefreshRecords {
  RefreshRecord *data{};
  size_t count{};
  ~RefreshRecords() {
    for (size_t i = 0; i < count; ++i) data[i].~RefreshRecord();
    KE_Free(data);
  }
};

bool EnsureDesktopSource() {
  {
    TopologyLock lock;
    if (g_desktop_source) return true;
  }
  auto created = kernel::CreateKObject<kernel::gfx::KVideoPresentSource>("DesktopSource");
  if (!created) return false;
  const auto source = kstd::move(*created);
  if (const auto inserted = KE_OB_InsertObject(KE_OB_GetGfxDirectory(), source);
      inserted) {
    return false;
  }
  {
    TopologyLock lock;
    g_desktop_source = source;
    ++g_topology_generation;
    if (!g_topology_generation) ++g_topology_generation;
  }
  return true;
}

void StartAdapter(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                  bool discoverOnly = false) {
  if (!discoverOnly) {
    KE_Print("GFX: Adapter {} started", adapter->name);

    adapter->state = kernel::KGraphicsAdapter::State::Probing;
    if (!EnsureDesktopSource()) {
      KE_Print("GFX: cannot create desktop source");
      adapter->state = kernel::KGraphicsAdapter::State::Terminated;
      return;
    }
    if (const kstd::error_code probed = adapter->Probe ? adapter->Probe(adapter) : kernel::KernelError::Success;
        probed) {
      KE_Print("GFX: Adapter {} probe failed: {}", adapter->name, probed.message());
      adapter->state = kernel::KGraphicsAdapter::State::Terminated;
      return;
    }
    adapter->state = kernel::KGraphicsAdapter::State::NeedsDisplayEnumerated;
  }

  KE_Print("GFX: Adapter {} display enumeration started", adapter->name);
  if (!adapter->ProbeDisplay) {
    KE_Print("GFX: Adapter {} display enumeration failed - method not present", adapter->name);
    adapter->state = kernel::KGraphicsAdapter::State::Terminated;
    return;
  }

  // Observe one record beyond the public limit so overflow is not truncated.
  constexpr uint32_t MaxDisplayHeads = kernel::gfx::VideoPresentRecordLimit + 1;
  const auto scratch = KE_CreateObject<AdapterHeadScratch>();
  if (!scratch) {
    KE_Print("GFX: cannot allocate head enumeration storage: {}", scratch.error().message());
    if (!discoverOnly) adapter->state = kernel::KGraphicsAdapter::State::Terminated;
    return;
  }

  for (uint32_t displayIndex = 0; displayIndex < MaxDisplayHeads; displayIndex++) {
    kernel::KGraphicsAdapterDisplayEnumInfo displayInfo{};
    displayInfo.displayIndex = displayIndex;

    (*scratch)->Reset();
    auto &outInfo = (*scratch)->info;
    if (const kstd::error_code probed = adapter->ProbeDisplay(adapter, displayInfo, outInfo);
        probed) {
      if (probed != kernel::KernelError::OutOfRange) {
        KE_Print("GFX: Adapter {} display head {} probe failed: {}", adapter->name, displayIndex, probed.message());
      }
      break;// Driver reported no more display heads
    }

    bool known = false;
    KE_OB_GetGfxDirectory()->Each<kernel::KDisplay>([&](const auto &display) {
      if (display->adapter == adapter && display->headIndex == displayIndex &&
          !display->removed)
        known = true;
    });
    if (known) continue;

    // Parse out the EDID if there's one so we have a fallback name
    if (outInfo.edidSize > kernel::MaxEdidSize) {
      outInfo.edidSize = kernel::MaxEdidSize;
    }

    auto &edid = (*scratch)->edid;
    if (outInfo.isPnP && kernel::Edid::ValidateHeader(kstd::span(outInfo.edid.data(), outInfo.edidSize))) {
      if (const kstd::error_code parsed = edid.Assign(kstd::span(outInfo.edid.data(), outInfo.edidSize));
          parsed) {
        KE_Print("GFX: Adapter {} display head {} has an unusable EDID: {}", adapter->name, displayIndex, parsed.message());
      }
    }

    // Determine base friendly display name: EDID Name -> Adapter Name -> "Display"
    kstd::static_string<64> baseName;
    if (edid.IsValid() && !edid.MonitorName().empty()) {
      baseName = edid.MonitorName();
    } else if (!outInfo.name.empty()) {
      baseName = outInfo.name;
    } else {
      // Make a name like "VgaDisplay" or "CirrusDisplay"
      kstd::format(baseName, "{}Display", adapter->name);
    }

    // Disambiguate Object Manager name conflicts (e.g., Display -> Display0)
    kstd::static_string<64> finalName;
    if (KE_OB_FindDirectChild(KE_OB_GetGfxDirectory(), baseName)) {
      const auto suffix = KE_OB_MakeUniqueName(KE_OB_GetGfxDirectory(), baseName);
      if (!suffix) {
        KE_Print("GFX: cannot assign display name: {}", suffix.error().message());
        adapter->state = kernel::KGraphicsAdapter::State::Terminated;
        return;
      }
      kstd::format(finalName, "{}{}", baseName, *suffix);
    } else {
      finalName = baseName;
    }

    // Instantiate and populate KDisplay object
    auto created = kernel::CreateKObject<kernel::KDisplay>(finalName);
    if (!created) {
      KE_Print("GFX: Failed to create display {} for adapter {}: {}", finalName, adapter->name, created.error().message());
      continue;
    }
    const auto display = kstd::move(*created);
    display->adapter = adapter;
    display->headIndex = displayIndex;
    display->isPresent = outInfo.isPresent;
    display->isMonitor = outInfo.isMonitor;
    display->edid = edid;

    // Let the driver bind its per-head entry points on the display we just
    // created. This is the only way a driver influences a KDisplay.
    if (!adapter->ConfigureDisplay) {
      KE_Print("GFX: Adapter {} has no ConfigureDisplay; display {} will be unusable", adapter->name, display->name);
    } else if (const kstd::error_code configured = adapter->ConfigureDisplay(adapter, display);
               configured) {
      KE_Print("GFX: Adapter {} discarded display head {}: {}", adapter->name, displayIndex, configured.message());
      continue;
    } else if (!display->GetModeInfo || !display->PrepareMode ||
               !display->ProgramMode || !display->FinishMode ||
               !display->QueryActualState || !display->Enable ||
               !display->Disable || !display->Validate) {
      KE_Print("GFX: Adapter {} left display {} without the target transaction interface",
               adapter->name, display->name);
    }

    // Decide what the monitor on this head can display: the EDID when the
    // head is PnP and the blob parsed, the registry-backed default monitor
    // otherwise.
    if (edid.IsValid()) {
      display->monitorCaps = kernel::gfx::capabilities_from_edid(edid);
      KE_Print("GFX: Display {} capabilities from EDID ({} timings)", display->name, display->monitorCaps.count);
    } else {
      display->monitorCaps = kernel::gfx::default_monitor_capabilities();
      KE_Print("GFX: Display {} capabilities from the default monitor ({} timings)",
               display->name, display->monitorCaps.count);
    }

    {
      TopologyLock lock;
      if (g_next_target_id == 0 || g_next_target_id == UINT32_MAX) {
        KE_Print("GFX: target identifier space exhausted");
        continue;
      }
      display->videoPresentTargetId = g_next_target_id;
      if (const kstd::error_code inserted = KE_OB_InsertObject(KE_OB_GetGfxDirectory(), display);
          inserted) {
        KE_Print("GFX: Failed to register display {} for adapter {}: {}", display->name, adapter->name, inserted.message());
        continue;
      }
      ++g_next_target_id;
      ++g_topology_generation;
      if (!g_topology_generation) ++g_topology_generation;
    }
  }

  adapter->state = kernel::KGraphicsAdapter::State::Running;

  KE_Print("GFX: Adapter {} display enumeration complete", adapter->name);

  // Log all registered displays for this adapter
  KE_OB_GetGfxDirectory()->Each<kernel::KDisplay>([&](const kernel::KObjectPtr<kernel::KDisplay> &display) {
    if (display->adapter == adapter) {
      KE_Print("GFX: Found display {} for adapter {}", display->name, adapter->name);
    }
  });
}

template<typename Func>
void FindAdaptersForDevice(const kernel::KDevicePtr<kernel::KDevice> &device, Func &&func) {
  KE_OB_GetGfxDirectory()->Each<kernel::KGraphicsAdapter>([&](const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) {
    if (adapter->device == device) {
      func(adapter);
    }
  });
}

// PnP events run on the event-pump thread, outside USER/topology locks.
void OnBackingDeviceEvent(const kernel::event::Source &source,
                          const kernel::event::Code &code,
                          const kernel::DataBuffer &userBuffer) {
  (void) userBuffer;

  if (const auto gfx = source.As<kernel::KDevice>()) {
    if (code.is(KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Started)) {
      FindAdaptersForDevice(gfx, [](const auto &adapter) {
        if (adapter->state == kernel::KGraphicsAdapter::State::Running)
          (void) KE_GFX_RefreshAdapter(adapter);
        else kernel::gfx::start_adapter(adapter);
      });
    } else if (code.is(KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Stopped)) {
      FindAdaptersForDevice(gfx, [](const auto &adapter) {
        KE_GFX_RemoveAdapter(adapter);
      });
    } else if (code.is(KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Changed)) {
      FindAdaptersForDevice(gfx, [](const auto &adapter) {
        (void) KE_GFX_RefreshAdapter(adapter);
      });
    }
  }
}
}// namespace

namespace kernel::gfx {
bool usable_mode(const KObjectPtr<KDisplay> &display, const KDisplayModeInfo &mode,
                 const KMonitorCapabilities &capabilities) noexcept {
  return display && mode.width && mode.height && mode.width <= 32767 && mode.height <= 32767 &&
         mode.pitch && mode.pitch <= INT32_MAX &&
         static_cast<uint64_t>(mode.pitch) * mode.height <= SIZE_MAX &&
         capabilities.Supports(mode);
}

bool usable_mode(const KObjectPtr<KDisplay> &display, const KDisplayModeInfo &mode) noexcept {
  return display && usable_mode(display, mode, display->monitorCaps);
}

kstd::expected<KDisplayModeInfo, kstd::error_code> enumerate_mode(const KObjectPtr<KDisplay> &display, uint32_t ordinal) noexcept {
  if (!display) return kstd::unexpected(KernelError::InvalidArgument);
  if (!display->GetModeInfo) return kstd::unexpected(KernelError::Unsupported);
  for (size_t index = 0; index <= VideoPresentRecordLimit; ++index) {
    auto mode = display->GetModeInfo(display, index);
    if (!mode) {
      if (mode.error() == KernelError::OutOfRange) return kstd::unexpected(KernelError::NotFound);
      return kstd::unexpected(mode.error());
    }
    if (!usable_mode(display, *mode)) continue;
    if (ordinal) {
      --ordinal;
    } else {
      mode->modeIndex = index;
      return *mode;
    }
  }
  return kstd::unexpected(KernelError::ResourceLimitExceeded);
}

namespace {
bool GetTargets(KObjectPtr<KDisplay> *targets, size_t capacity, size_t &count) {
  count = 0;
  bool overflow = false;
  KE_OB_GetGfxDirectory()->Each<KDisplay>([&](const KObjectPtr<KDisplay> &target) {
    if (!target->videoPresentTargetId || target->removed) return;
    if (count == capacity) {
      overflow = true;
      return;
    }
    targets[count++] = target;
  });
  return !overflow;
}

KObjectPtr<KDisplay> FindTarget(uint32_t targetId) {
  KObjectPtr<KDisplay> found;
  KE_OB_GetGfxDirectory()->Each<KDisplay>([&](const KObjectPtr<KDisplay> &target) {
    if (!target->removed && target->videoPresentTargetId == targetId) found = target;
  });
  return found;
}

template<typename Index>
uint32_t ModeIdFromIndex(Index index) noexcept {
  if constexpr (sizeof(Index) == sizeof(uint32_t)) return index + 1;
  else return static_cast<uint32_t>(index + 1);
}

kstd::error_code CollectTargetModes(const KObjectPtr<KDisplay> &target,
                                    VideoPresentTargetModeInfo *modes,
                                    size_t capacity, size_t &count) {
  count = 0;
  if (!target || !target->GetModeInfo || !target->PrepareMode ||
      !target->ProgramMode || !target->FinishMode ||
      !target->QueryActualState || !target->Enable || !target->Disable ||
      !target->Validate) return {};
  for (uint32_t ordinal = 0; ordinal <= VideoPresentRecordLimit; ++ordinal) {
    const auto mode = enumerate_mode(target, ordinal);
    if (!mode) {
      if (mode.error() == KernelError::NotFound) return {};
      return mode.error();
    }
    if (ordinal == VideoPresentRecordLimit) return KernelError::ResourceLimitExceeded;
    if (mode->modeIndex >= UINT32_MAX) return KernelError::OutOfRange;
    if (modes && count >= capacity) return KernelError::Retry;
    if (modes) modes[count] = VideoPresentTargetModeInfo{
        .targetId = target->videoPresentTargetId,
        .modeId = ModeIdFromIndex(mode->modeIndex),
        .mode = *mode};
    ++count;
  }
  return KernelError::ResourceLimitExceeded;
}

bool SameSourceMode(const VideoPresentSourceModeInfo &left,
                    const KDisplayModeInfo &right) {
  return left.width == right.width && left.height == right.height &&
         left.colorDepth == right.bpp && left.pixelFormat == right.pixelFormat;
}

kstd::error_code CheckGeneration(uint32_t generation) {
  return generation == g_topology_generation ? KernelError::Success : KernelError::Retry;
}

struct ModeReservationGuard {
  KObjectPtr<KDisplay> target;
  KDisplayModeReservation &reservation;
  bool finished{false};

  ModeReservationGuard(const KObjectPtr<KDisplay> &targetArg,
                       KDisplayModeReservation &reservationArg)
      : target(targetArg), reservation(reservationArg) {}

  ~ModeReservationGuard() { Finish(false); }

  void Finish(bool committed) {
    if (finished) return;
    if (target && target->FinishMode)
      target->FinishMode(target, reservation, committed);
    reservation.driverData = nullptr;
    reservation.binding.backing = nullptr;
    finished = true;
  }
};
}// namespace

void refresh_observed_topology() noexcept {
  ++g_topology_generation;
  if (!g_topology_generation) ++g_topology_generation;
}

namespace {
void *SnapshotAllocate(size_t bytes) {
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
  if (g_topology_depth || (g_snapshot_spinlock_observer && g_snapshot_spinlock_observer()))
    ++g_snapshot_locked_allocations;
  if (!g_snapshot_allocations_until_failure) return nullptr;
  if (g_snapshot_allocations_until_failure != SIZE_MAX)
    --g_snapshot_allocations_until_failure;
#endif
  void *memory = KE_Malloc(bytes);
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
  if (memory) ++g_snapshot_live_allocations;
#endif
  return memory;
}
void SnapshotFree(void *memory) {
  if (!memory) return;
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
  if (g_topology_depth || (g_snapshot_spinlock_observer && g_snapshot_spinlock_observer()))
    ++g_snapshot_locked_allocations;
  --g_snapshot_live_allocations;
#endif
  KE_Free(memory);
}
void ReleaseSnapshot(VideoPresentSnapshot *snapshot) {
  if (snapshot && snapshot->references.fetch_sub(1) == 1) {
    snapshot->~VideoPresentSnapshot();
    SnapshotFree(snapshot);
  }
}

template<typename T>
kstd::error_code AllocateRecords(VideoPresentSnapshot &snapshot, T *&records,
                                 size_t count) {
  if (count > VideoPresentRecordLimit || count > SIZE_MAX / sizeof(T))
    return KernelError::ResourceLimitExceeded;
  const size_t bytes = count * sizeof(T);
  if (bytes > VideoPresentSnapshotBudgetBytes - snapshot.allocatedBytes)
    return KernelError::ResourceLimitExceeded;
  if (!count) return {};
  records = static_cast<T *>(SnapshotAllocate(bytes));
  if (!records) return KernelError::OutOfMemory;
  for (size_t i = 0; i < count; ++i) new (&records[i]) T{};
  snapshot.allocatedBytes += bytes;
  return {};
}

template<typename T>
void FreeRecords(T *records, size_t count) {
  if (!records) return;
  for (size_t i = 0; i < count; ++i) records[i].~T();
  SnapshotFree(records);
}

void Observe(const KObjectPtr<KDisplay> &target, VideoPresentSnapshot::Observation &out) {
  const auto actual = !target->removed && target->QueryActualState
      ? target->QueryActualState(target)
      : kstd::expected<KDisplayActualState, kstd::error_code>(
          kstd::unexpected(KernelError::InvalidState));
  out.capabilities = target->monitorCaps;
  out.resourceGeneration = target->adapter ? target->adapter->resourceGeneration : 0;
  out.adapterCaps = target->adapter ? static_cast<uint32_t>(target->adapter->caps) : 0;
  out.present = target->isPresent;
  out.degraded = target->topologyDegraded;
  out.actualKnown = actual && actual->known;
  out.actualEnabled = actual && actual->enabled;
  out.actualMode = actual ? actual->modeIndex : 0;
}

bool SameObservation(const VideoPresentSnapshot::Observation &before,
                     const KObjectPtr<KDisplay> &target) {
  const auto &a = before.capabilities;
  const auto &b = target->monitorCaps;
  if (target->removed || before.present != target->isPresent ||
      before.degraded != target->topologyDegraded ||
      before.resourceGeneration != (target->adapter ? target->adapter->resourceGeneration : 0) ||
      before.adapterCaps != (target->adapter ? static_cast<uint32_t>(target->adapter->caps) : 0) ||
      a.count != b.count || a.maxWidth != b.maxWidth || a.maxHeight != b.maxHeight ||
      a.maxRefreshRate != b.maxRefreshRate || a.isDefault != b.isDefault)
    return false;
  for (size_t i = 0; i < a.count && i < KMonitorCapabilities::MaxTimings; ++i)
    if (a.timings[i].width != b.timings[i].width ||
        a.timings[i].height != b.timings[i].height ||
        a.timings[i].refreshRate != b.timings[i].refreshRate) return false;
  return true;
}

bool SnapshotMatches(const VideoPresentSnapshot &snapshot) {
  size_t targetIndex = 0;
  bool sameTargets = true;
  KE_OB_GetGfxDirectory()->Each<KDisplay>([&](const auto &target) {
    if (!target->videoPresentTargetId || target->removed) return;
    if (targetIndex >= snapshot.topology.targetCount ||
        snapshot.targetReferences[targetIndex] != target) sameTargets = false;
    ++targetIndex;
  });
  if (!sameTargets || targetIndex != snapshot.topology.targetCount) return false;
  size_t offset = 0;
  for (uint32_t t = 0; t < snapshot.topology.targetCount; ++t) {
    const auto &target = snapshot.targetReferences[t];
    if (!SameObservation(snapshot.observations[t], target)) return false;
    const auto actual = target->QueryActualState
        ? target->QueryActualState(target)
        : kstd::expected<KDisplayActualState, kstd::error_code>(
            kstd::unexpected(KernelError::InvalidState));
    const auto &prior = snapshot.observations[t];
    if (prior.actualKnown != (actual && actual->known) ||
        prior.actualEnabled != (actual && actual->enabled) ||
        prior.actualMode != (actual ? actual->modeIndex : 0)) return false;
    if (!target->GetModeInfo || !target->PrepareMode || !target->ProgramMode ||
        !target->FinishMode || !target->QueryActualState || !target->Enable ||
        !target->Disable || !target->Validate) continue;
    for (uint32_t i = 0; i < snapshot.targets[t].modeCount; ++i) {
      const auto mode = enumerate_mode(target, i);
      if (!mode || offset >= snapshot.topology.targetModeCount ||
          !(snapshot.targetModes[offset].mode == *mode)) return false;
      ++offset;
    }
    const auto extra = enumerate_mode(target, snapshot.targets[t].modeCount);
    if (extra || extra.error() != KernelError::NotFound) return false;
  }
  return offset == snapshot.topology.targetModeCount;
}

void CapturePaths(VideoPresentSnapshot &snapshot) {
  auto &info = snapshot.topology;
  info.pathCount = g_desktop_source && g_desktop_source->active ? 1u : 0u;
  snapshot.pathsValid = true;
  info.degraded = 0;
  if (info.pathCount && snapshot.pathCapacity) snapshot.paths[0] = g_active_path;
  for (uint32_t i = 0; i < info.targetCount; ++i) {
    const auto &target = snapshot.targetReferences[i];
    if (target->topologyDegraded ||
        (target->isAttachedToDesktop &&
         (!g_desktop_source || !g_desktop_source->active || g_desktop_source->target != target)))
      info.degraded = 1;
  }
  if ((g_desktop_source && g_desktop_source->active) != g_active_path_valid)
    info.degraded = 1;
  if (info.pathCount) {
    const auto active = g_desktop_source->target;
    const auto actual = active && !active->removed && active->QueryActualState
        ? active->QueryActualState(active)
        : kstd::expected<KDisplayActualState, kstd::error_code>(
            kstd::unexpected(KernelError::InvalidState));
    if (!active || !g_desktop_source->surface ||
        active->surface != g_desktop_source->surface ||
        active->currentMode.modeIndex != g_desktop_source->currentMode.modeIndex ||
        !g_active_path_valid || g_active_path.targetId != active->videoPresentTargetId ||
        !actual || !actual->known || !actual->enabled ||
        actual->modeIndex != active->currentMode.modeIndex) {
      info.degraded = 1;
      snapshot.pathsValid = false;
    }
  }
}

kstd::expected<VideoPresentSnapshotLease, kstd::error_code> NewSnapshot() {
  void *memory = SnapshotAllocate(sizeof(VideoPresentSnapshot));
  if (!memory) return kstd::unexpected(KernelError::OutOfMemory);
  auto *snapshot = new (memory) VideoPresentSnapshot{};
  snapshot->allocatedBytes = sizeof(VideoPresentSnapshot);
  return VideoPresentSnapshotLease(snapshot);
}

kstd::expected<VideoPresentSnapshotLease, kstd::error_code>
CloneSnapshot(const VideoPresentSnapshot &source) {
  auto created = NewSnapshot();
  if (!created) return kstd::unexpected(created.error());
  auto &copy = const_cast<VideoPresentSnapshot &>(**created);
  copy.topology = source.topology;
  copy.pathCapacity = 1;
  const auto copyRecords = [&](auto *&output, const auto *input, size_t count) -> kstd::error_code {
    if (auto error = AllocateRecords(copy, output, count); error) return error;
    for (size_t i = 0; i < count; ++i) output[i] = input[i];
    return {};
  };
  if (auto error = copyRecords(copy.sources, source.sources, source.topology.sourceCount); error)
    return kstd::unexpected(error);
  if (auto error = copyRecords(copy.sourceModes, source.sourceModes, source.topology.sourceModeCount); error)
    return kstd::unexpected(error);
  if (auto error = copyRecords(copy.targets, source.targets, source.topology.targetCount); error)
    return kstd::unexpected(error);
  if (auto error = copyRecords(copy.targetModes, source.targetModes, source.topology.targetModeCount); error)
    return kstd::unexpected(error);
  if (auto error = copyRecords(copy.targetReferences, source.targetReferences, source.topology.targetCount); error)
    return kstd::unexpected(error);
  if (auto error = copyRecords(copy.observations, source.observations, source.topology.targetCount); error)
    return kstd::unexpected(error);
  if (auto error = AllocateRecords(copy, copy.paths, copy.pathCapacity); error)
    return kstd::unexpected(error);
  return *created;
}

kstd::expected<VideoPresentSnapshotLease, kstd::error_code> BuildSnapshot() {
  auto created = NewSnapshot();
  if (!created) return kstd::unexpected(created.error());
  auto &snapshot = const_cast<VideoPresentSnapshot &>(**created);
  bool targetLimitExceeded = false;
  {
    DriverLock drivers;
    TopologyLock lock;
    snapshot.topology.generation = g_topology_generation;
    KE_OB_GetGfxDirectory()->Each<KDisplay>([&](const auto &target) {
      if (!target->videoPresentTargetId || target->removed) return;
      if (snapshot.topology.targetCount == VideoPresentRecordLimit) {
        targetLimitExceeded = true;
        return;
      }
      ++snapshot.topology.targetCount;
    });
    snapshot.topology.sourceCount = g_desktop_source ? 1u : 0u;
    snapshot.topology.pathCount = g_desktop_source && g_desktop_source->active ? 1u : 0u;
  }
  auto &info = snapshot.topology;
  if (targetLimitExceeded) return kstd::unexpected(KernelError::ResourceLimitExceeded);
  if (auto error = AllocateRecords(snapshot, snapshot.targetReferences, info.targetCount); error)
    return kstd::unexpected(error);
  if (auto error = AllocateRecords(snapshot, snapshot.observations, info.targetCount); error)
    return kstd::unexpected(error);
  if (auto error = AllocateRecords(snapshot, snapshot.targets, info.targetCount); error)
    return kstd::unexpected(error);
  if (auto error = AllocateRecords(snapshot, snapshot.sources, info.sourceCount); error)
    return kstd::unexpected(error);
  snapshot.pathCapacity = info.pathCount;
  if (auto error = AllocateRecords(snapshot, snapshot.paths, snapshot.pathCapacity); error)
    return kstd::unexpected(error);
  {
    DriverLock drivers;
    TopologyLock lock;
    if (CheckGeneration(info.generation)) return kstd::unexpected(KernelError::Retry);
    size_t count = 0;
    if (!GetTargets(snapshot.targetReferences, info.targetCount, count) ||
        count != info.targetCount) return kstd::unexpected(KernelError::Retry);
    if (info.sourceCount) snapshot.sources[0] = g_desktop_source;
    for (uint32_t i = 0; i < info.targetCount; ++i)
      Observe(snapshot.targetReferences[i], snapshot.observations[i]);
  }
  {
    DriverLock drivers;
    for (uint32_t i = 0; i < info.targetCount; ++i) {
      if (!SameObservation(snapshot.observations[i], snapshot.targetReferences[i]))
        return kstd::unexpected(KernelError::Retry);
      size_t count = 0;
      if (const auto error = CollectTargetModes(snapshot.targetReferences[i], nullptr, 0, count); error)
        return kstd::unexpected(error);
      if (count > VideoPresentRecordLimit - info.targetModeCount)
        return kstd::unexpected(KernelError::ResourceLimitExceeded);
      const auto &target = snapshot.targetReferences[i];
      snapshot.targets[i] = {.targetId = target->videoPresentTargetId,
          .connected = target->isPresent, .modeCount = static_cast<uint32_t>(count),
          .maxWidth = target->monitorCaps.maxWidth, .maxHeight = target->monitorCaps.maxHeight,
          .maxRefreshHz = target->monitorCaps.maxRefreshRate};
      info.targetModeCount += static_cast<uint32_t>(count);
    }
  }
  if (auto error = AllocateRecords(snapshot, snapshot.targetModes, info.targetModeCount); error)
    return kstd::unexpected(error);
  {
    DriverLock drivers;
    size_t offset = 0;
    for (uint32_t i = 0; i < info.targetCount; ++i) {
      if (!SameObservation(snapshot.observations[i], snapshot.targetReferences[i]))
        return kstd::unexpected(KernelError::Retry);
      size_t count = 0;
      auto *records = snapshot.targetModes ? snapshot.targetModes + offset : nullptr;
      if (auto error = CollectTargetModes(snapshot.targetReferences[i], records,
                                          snapshot.targets[i].modeCount, count); error)
        return kstd::unexpected(error);
      if (count != snapshot.targets[i].modeCount) return kstd::unexpected(KernelError::Retry);
      offset += count;
    }
  }
  for (uint32_t i = 0; i < info.targetModeCount; ++i) {
    bool duplicate = false;
    for (uint32_t j = 0; j < i; ++j) {
      const auto &a = snapshot.targetModes[i].mode;
      const auto &b = snapshot.targetModes[j].mode;
      if (a.width == b.width && a.height == b.height && a.bpp == b.bpp &&
          a.pixelFormat == b.pixelFormat) duplicate = true;
    }
    if (!duplicate) {
      if (info.sourceModeCount == VideoPresentRecordLimit)
        return kstd::unexpected(KernelError::ResourceLimitExceeded);
      ++info.sourceModeCount;
    }
  }
  if (auto error = AllocateRecords(snapshot, snapshot.sourceModes, info.sourceModeCount); error)
    return kstd::unexpected(error);
  size_t sourceCount = 0;
  for (uint32_t i = 0; i < info.targetModeCount; ++i) {
    const auto &mode = snapshot.targetModes[i].mode;
    bool duplicate = false;
    for (size_t j = 0; j < sourceCount; ++j)
      if (SameSourceMode(snapshot.sourceModes[j], mode)) duplicate = true;
    if (!duplicate) {
      snapshot.sourceModes[sourceCount] = {.modeId = ModeIdFromIndex(sourceCount),
          .width = mode.width, .height = mode.height, .colorDepth = mode.bpp,
          .pixelFormat = mode.pixelFormat};
      ++sourceCount;
    }
  }
  VideoPresentSnapshotLease retired;
  {
    DriverLock drivers;
    if (!SnapshotMatches(snapshot)) return kstd::unexpected(KernelError::Retry);
    TopologyLock lock;
    if (CheckGeneration(info.generation)) return kstd::unexpected(KernelError::Retry);
    CapturePaths(snapshot);
    if (info.pathCount > snapshot.pathCapacity) return kstd::unexpected(KernelError::Retry);
    retired = VideoPresentSnapshotLease(g_snapshot);
    snapshot.references.fetch_add(1);
    g_snapshot = &snapshot;
  }
  return *created;
}
}

VideoPresentSnapshot::~VideoPresentSnapshot() {
  FreeRecords(sources, topology.sourceCount);
  FreeRecords(sourceModes, topology.sourceModeCount);
  FreeRecords(targets, topology.targetCount);
  FreeRecords(targetModes, topology.targetModeCount);
  FreeRecords(paths, pathCapacity);
  FreeRecords(targetReferences, topology.targetCount);
  FreeRecords(observations, topology.targetCount);
}
VideoPresentSnapshotLease::VideoPresentSnapshotLease(const VideoPresentSnapshotLease &other) noexcept
    : value_(other.value_) {
  if (value_) value_->references.fetch_add(1);
}
VideoPresentSnapshotLease &VideoPresentSnapshotLease::operator=(const VideoPresentSnapshotLease &other) noexcept {
  if (other.value_) other.value_->references.fetch_add(1);
  ReleaseSnapshot(value_);
  value_ = other.value_;
  return *this;
}
VideoPresentSnapshotLease::~VideoPresentSnapshotLease() { ReleaseSnapshot(value_); }
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
void video_present_test_fail_allocation_after(size_t count) noexcept {
  g_snapshot_allocations_until_failure = count;
}
void video_present_test_set_spinlock_observer(bool (*observer)()) noexcept {
  g_snapshot_spinlock_observer = observer;
}
size_t video_present_test_live_allocations() noexcept { return g_snapshot_live_allocations; }
size_t video_present_test_locked_allocations() noexcept { return g_snapshot_locked_allocations; }
uint32_t video_present_test_generation() noexcept {
  TopologyLock lock;
  return g_topology_generation;
}
void video_present_test_before_commit_decision(void (*hook)()) noexcept {
  g_before_commit_decision = hook;
}
bool video_present_test_callback_gate_held() noexcept { return g_callback_gate_depth != 0; }
#endif

kstd::expected<VideoPresentSnapshotLease, kstd::error_code>
acquire_published_video_present_snapshot(uint32_t generation) noexcept {
  TopologyLock lock;
  if (!generation || generation != g_topology_generation || !g_snapshot ||
      generation != g_snapshot->topology.generation)
    return kstd::unexpected(KernelError::Retry);
  g_snapshot->references.fetch_add(1);
  return VideoPresentSnapshotLease(g_snapshot);
}

kstd::expected<VideoPresentSnapshotLease, kstd::error_code>
acquire_video_present_snapshot(uint32_t generation) noexcept {
  VideoPresentSnapshotLease current;
  {
    TopologyLock lock;
    if (generation && generation != g_topology_generation)
      return kstd::unexpected(KernelError::Retry);
    if (g_snapshot && g_snapshot->topology.generation == g_topology_generation) {
      g_snapshot->references.fetch_add(1);
      current = VideoPresentSnapshotLease(g_snapshot);
    }
  }
  if (current) {
    DriverLock drivers;
    const bool matches = SnapshotMatches(*current);
    TopologyLock lock;
    if (!matches && current->topology.generation == g_topology_generation)
      refresh_observed_topology();
    if (generation && generation != g_topology_generation)
      return kstd::unexpected(KernelError::Retry);
    if (matches && current->topology.generation == g_topology_generation) {
      if (generation && generation != current->topology.generation)
        return kstd::unexpected(KernelError::Retry);
      return current;
    }
  }
  auto built = BuildSnapshot();
  if (!built) return kstd::unexpected(built.error());
  if (generation && generation != (*built)->topology.generation)
    return kstd::unexpected(KernelError::Retry);
  return *built;
}

namespace {
// Caller owns callback serialization. Never acquire that non-recursive gate here.
KObjectPtr<KDisplay> ActiveTargetUnderGate() {
  KObjectPtr<KDisplay> target;
  size_t modeIndex{};
  {
    TopologyLock lock;
    if (!g_desktop_source || !g_desktop_source->active || !g_desktop_source->target ||
        !g_desktop_source->target->isPresent || g_desktop_source->target->removed ||
        !g_desktop_source->target->QueryActualState)
      return nullptr;
    target = g_desktop_source->target;
    modeIndex = g_desktop_source->currentMode.modeIndex;
  }
  const auto actual = target->QueryActualState(target);
  return actual && actual->known && actual->enabled &&
                 actual->modeIndex == modeIndex
             ? target
             : nullptr;
}
}

KObjectPtr<KDisplay> active_video_present_target() noexcept {
  DriverLock drivers;
  return ActiveTargetUnderGate();
}

bool has_video_present_targets() noexcept {
  const auto snapshot = acquire_video_present_snapshot();
  if (!snapshot) return false;
  for (uint32_t i = 0; i < (*snapshot)->topology.targetCount; ++i)
    if ((*snapshot)->targets[i].connected && (*snapshot)->targets[i].modeCount)
      return true;
  return false;
}

 kstd::error_code get_video_present_topology(VideoPresentTopologyInfo &out) noexcept {
  const auto snapshot = acquire_video_present_snapshot();
  if (!snapshot) return snapshot.error();
  out = (*snapshot)->topology;
  return {};
}

kstd::expected<VideoPresentSourceModeInfo, kstd::error_code>
video_present_source_mode(uint32_t generation, uint32_t sourceId, uint32_t index) noexcept {
  const auto snapshot = acquire_video_present_snapshot(generation);
  if (!snapshot) return kstd::unexpected(snapshot.error());
  if (sourceId != DesktopSourceId) return kstd::unexpected(KernelError::NotFound);
  if (index >= (*snapshot)->topology.sourceModeCount) return kstd::unexpected(KernelError::NotFound);
  return (*snapshot)->sourceModes[index];
}

kstd::expected<VideoPresentTargetInfo, kstd::error_code>
video_present_target(uint32_t generation, uint32_t index) noexcept {
  const auto snapshot = acquire_video_present_snapshot(generation);
  if (!snapshot) return kstd::unexpected(snapshot.error());
  if (index >= (*snapshot)->topology.targetCount) return kstd::unexpected(KernelError::NotFound);
  return (*snapshot)->targets[index];
}

kstd::expected<VideoPresentTargetModeInfo, kstd::error_code>
video_present_target_mode(uint32_t generation, uint32_t targetId, uint32_t index) noexcept {
  const auto snapshot = acquire_video_present_snapshot(generation);
  if (!snapshot) return kstd::unexpected(snapshot.error());
  for (uint32_t i = 0; i < (*snapshot)->topology.targetModeCount; ++i)
    if ((*snapshot)->targetModes[i].targetId == targetId) {
      if (!index) return (*snapshot)->targetModes[i];
      --index;
    }
  return kstd::unexpected(KernelError::NotFound);
}

kstd::expected<VideoPresentPathInfo, kstd::error_code>
video_present_path(uint32_t generation, uint32_t index) noexcept {
  const auto snapshot = acquire_video_present_snapshot(generation);
  if (!snapshot) return kstd::unexpected(snapshot.error());
  if (index >= (*snapshot)->topology.pathCount)
    return kstd::unexpected(KernelError::NotFound);
  if (!(*snapshot)->pathsValid) return kstd::unexpected(KernelError::InvalidState);
  return (*snapshot)->paths[index];
}

namespace {
kstd::expected<KObjectPtr<KDisplay>, kstd::error_code>
ResolvePathLocked(const VideoPresentSnapshot &snapshot, uint32_t generation, const VideoPresentPathInfo &path,
                  size_t &driverModeIndex) noexcept {
  {
    TopologyLock lock;
    if (const auto error = CheckGeneration(generation); error) return kstd::unexpected(error);
  }
  if (!g_desktop_source || path.sourceId != g_desktop_source->sourceId ||
      path.sourceX != 0 || path.sourceY != 0 ||
      path.flags != 0 || path.width == 0 || path.height == 0 ||
      path.width > INT32_MAX || path.height > INT32_MAX)
    return kstd::unexpected(KernelError::InvalidArgument);
  const auto target = FindTarget(path.targetId);
  if (!target || !target->isPresent || !target->GetModeInfo ||
      !target->PrepareMode || !target->ProgramMode || !target->FinishMode ||
      !target->QueryActualState || !target->Enable || !target->Disable ||
      !target->Validate)
    return kstd::unexpected(KernelError::Unsupported);
  if (const auto error = target->Validate(target); error)
    return kstd::unexpected(error);

  const VideoPresentSourceModeInfo *sourceMode = nullptr;
  for (size_t index = 0; index < snapshot.topology.sourceModeCount; ++index)
    if (snapshot.sourceModes[index].modeId == path.sourceModeId) sourceMode = &snapshot.sourceModes[index];
  if (!sourceMode || sourceMode->width != path.width || sourceMode->height != path.height)
    return kstd::unexpected(KernelError::InvalidArgument);

  for (size_t index = 0; index < snapshot.topology.targetModeCount; ++index) {
    const auto &candidate = snapshot.targetModes[index];
    if (candidate.targetId != path.targetId || candidate.modeId != path.targetModeId) continue;
    if (candidate.mode.width != path.width || candidate.mode.height != path.height ||
        candidate.mode.bpp != sourceMode->colorDepth ||
        candidate.mode.pixelFormat != sourceMode->pixelFormat)
      return kstd::unexpected(KernelError::Unsupported);
    driverModeIndex = candidate.mode.modeIndex;
    return target;
  }
  return kstd::unexpected(KernelError::NotFound);
}
}

kstd::expected<KObjectPtr<KDisplay>, kstd::error_code>
video_present_resolve_path(uint32_t generation, const VideoPresentPathInfo &path,
                           size_t &driverModeIndex) noexcept {
  const auto snapshot = acquire_video_present_snapshot(generation);
  if (!snapshot) return kstd::unexpected(snapshot.error());
  DriverLock drivers;
  return ResolvePathLocked(**snapshot, generation, path, driverModeIndex);
}

void video_present_release_reservation(const KObjectPtr<KDisplay> &target,
                                       KDisplayModeReservation &reservation) noexcept {
  DriverLock drivers;
  ModeReservationGuard guard(target, reservation);
}

kstd::error_code video_present_prepare_path(
    uint32_t generation, const VideoPresentPathInfo &path,
    KObjectPtr<KDisplay> &target, size_t &driverModeIndex,
    KDisplayModeReservation &reservation, VideoPresentSnapshotLease *lease) noexcept {
  const auto snapshot = acquire_video_present_snapshot(generation);
  if (!snapshot) return snapshot.error();
  DriverLock drivers;
  reservation = {};
  target = nullptr;
  size_t modeIndex = 0;
  const auto resolved = ResolvePathLocked(**snapshot, generation, path, modeIndex);
  if (!resolved) return resolved.error();
  target = *resolved;

  const auto finish = [&](bool committed) {
    if (target->FinishMode)
      target->FinishMode(target, reservation, committed);
    reservation.driverData = nullptr;
    reservation.binding.backing = nullptr;
  };
  if (!target->PrepareMode || !target->FinishMode)
    return KernelError::Unsupported;
  if (const auto error = target->PrepareMode(target, modeIndex, reservation); error) {
    finish(false);
    return error;
  }
  const auto mode = target->GetModeInfo
      ? target->GetModeInfo(target, modeIndex)
      : kstd::expected<KDisplayModeInfo, kstd::error_code>(
            kstd::unexpected(KernelError::Unsupported));
  if (!mode || reservation.modeIndex != modeIndex ||
      reservation.mode.width != mode->width ||
      reservation.mode.height != mode->height ||
      reservation.mode.pitch != mode->pitch ||
      reservation.mode.bpp != mode->bpp ||
      reservation.mode.refreshRate != mode->refreshRate ||
      reservation.mode.pixelFormat != mode->pixelFormat ||
      !usable_mode(target, reservation.mode) || !reservation.binding.backing ||
      reservation.binding.backing->size <
          static_cast<size_t>(reservation.mode.pitch) *
              reservation.mode.height) {
    finish(false);
    return mode ? KernelError::InvalidState : mode.error();
  }
  reservation.mode.modeIndex = modeIndex;
  driverModeIndex = modeIndex;
  if (lease) *lease = *snapshot;
  return KernelError::Success;
}

void start_adapter(const KObjectPtr<KGraphicsAdapter> &adapter) noexcept {
  if (!adapter) return;
  DriverLock drivers;
  if (adapter->device && adapter->device->pnpState == KPnpState::Removed) return;
  StartAdapter(adapter);
}

void wait_for_drivers() noexcept {

  while (true) {
    bool all_adapters_running = true;

    KE_OB_GetGfxDirectory()->Each<KGraphicsAdapter>([&](const KObjectPtr<KGraphicsAdapter> &adapter) {
      if (adapter->state != KGraphicsAdapter::State::Running && adapter->state != KGraphicsAdapter::State::Terminated) {
        all_adapters_running = false;
      }
    });

    if (all_adapters_running)
      break;

    KE_SCHED_Yield();
  }
}
}// namespace kernel::gfx

namespace {
void InvalidateActiveTarget(const kernel::KObjectPtr<kernel::KDisplay> &target) {
  if (!g_desktop_source || g_desktop_source->target != target) return;
  g_desktop_source->target = nullptr;
  g_desktop_source->surface = nullptr;
  g_desktop_source->currentMode = {};
  g_desktop_source->active = false;
  g_active_path_valid = false;
}
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_GFX_RefreshAdapter(
    const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) noexcept {
  if (!adapter || !adapter->ProbeDisplay) return kernel::KernelError::InvalidArgument;
  if (adapter->device && adapter->device->pnpState == kernel::KPnpState::Removed)
    return kernel::KernelError::DeviceNotReady;
  const auto scratch = KE_CreateObject<AdapterHeadScratch>();
  if (!scratch) return scratch.error();
  RefreshRecords records;
  uint32_t generation{};
  {
    DriverLock drivers;
    TopologyLock lock;
    generation = g_topology_generation;
    KE_OB_GetGfxDirectory()->Each<kernel::KDisplay>([&](const auto &target) {
      if (target->adapter == adapter && !target->removed) ++records.count;
    });
  }
  if (records.count > kernel::gfx::VideoPresentRecordLimit ||
      records.count > SIZE_MAX / sizeof(RefreshRecord)) {
    records.count = 0;
    return kernel::KernelError::ResourceLimitExceeded;
  }
  if (records.count) {
    records.data = static_cast<RefreshRecord *>(KE_Malloc(records.count * sizeof(RefreshRecord)));
    if (!records.data) {
      records.count = 0;
      return kernel::KernelError::OutOfMemory;
    }
    for (size_t i = 0; i < records.count; ++i) new (&records.data[i]) RefreshRecord{};
  }
  kstd::error_code result{};
  {
    DriverLock drivers;
    if (adapter->device && adapter->device->pnpState == kernel::KPnpState::Removed)
      return kernel::KernelError::DeviceNotReady;
    {
      TopologyLock lock;
      if (generation != g_topology_generation) return kernel::KernelError::Retry;
      size_t index = 0;
      bool overflow = false;
      KE_OB_GetGfxDirectory()->Each<kernel::KDisplay>([&](const auto &target) {
        if (target->adapter != adapter || target->removed) return;
        if (index == records.count) { overflow = true; return; }
        records.data[index++].target = target;
      });
      if (overflow || index != records.count) return kernel::KernelError::Retry;
    }
    for (size_t i = 0; i < records.count; ++i) {
      auto &record = records.data[i];
      const auto &target = record.target;
      (*scratch)->Reset();
      auto &info = (*scratch)->info;
      const kernel::KGraphicsAdapterDisplayEnumInfo ordinal{target->headIndex};
      const auto probed = adapter->ProbeDisplay(adapter, ordinal, info);
      if (probed && probed != kernel::KernelError::OutOfRange) {
        result = probed;
        continue;
      }
      record.valid = true;
      record.present = !probed && info.isPresent;
      record.monitor = !probed && info.isMonitor;
      if (info.isPnP && info.edidSize <= kernel::MaxEdidSize &&
          kernel::Edid::ValidateHeader(kstd::span(info.edid.data(), info.edidSize)))
        (void) record.edid.Assign(kstd::span(info.edid.data(), info.edidSize));
      record.capabilities = record.edid.IsValid()
          ? kernel::gfx::capabilities_from_edid(record.edid)
          : kernel::gfx::default_monitor_capabilities();
      if (target->isAttachedToDesktop) {
        bool driverModeSupported = false;
        if (target->GetModeInfo) {
          auto mode = target->GetModeInfo(target, target->currentMode.modeIndex);
          if (mode) mode->modeIndex = target->currentMode.modeIndex;
          driverModeSupported = mode && *mode == target->currentMode &&
                                kernel::gfx::usable_mode(target, *mode);
        }
        record.currentModeSupported =
            driverModeSupported &&
            kernel::gfx::usable_mode(target, target->currentMode, record.capabilities);
      }
    }
  }
  {
    kernel::user::Guard scene;
    kernel::user::SceneDrawGuard drawing;
    {
      DriverLock drivers;
      {
        TopologyLock lock;
        if (generation != g_topology_generation) return kernel::KernelError::Retry;
      }
      if (adapter->device && adapter->device->pnpState == kernel::KPnpState::Removed)
        return kernel::KernelError::DeviceNotReady;
      ++adapter->resourceGeneration;
      for (size_t i = 0; i < records.count; ++i) {
        const auto &record = records.data[i];
        if (!record.valid) continue;
        const auto &target = record.target;
        target->isPresent = record.present;
        target->isMonitor = record.monitor;
        target->edid = record.edid;
        target->monitorCaps = record.capabilities;
        if (!target->isPresent || (target->isAttachedToDesktop &&
            !record.currentModeSupported)) {
          const auto disabled = target->Disable ? target->Disable(target)
              : kernel::KernelError::Unsupported;
          const auto actual = target->QueryActualState
              ? target->QueryActualState(target)
              : kstd::expected<kernel::KDisplayActualState, kstd::error_code>(
                    kstd::unexpected(kernel::KernelError::Unsupported));
          target->isAttachedToDesktop = actual && actual->known && actual->enabled;
          target->topologyDegraded = disabled || !actual || !actual->known ||
                                     actual->enabled;
          InvalidateActiveTarget(target);
        }
      }
      {
        TopologyLock lock;
        kernel::gfx::refresh_observed_topology();
      }
    }
    const auto active = kernel::gfx::active_video_present_target();
    kernel::user::display_changed_locked(active ? active->currentMode.bpp : 0);
  }
  {
    DriverLock drivers;
    if (!adapter->device || adapter->device->pnpState != kernel::KPnpState::Removed)
      StartAdapter(adapter, true);
  }
  return result;
}

namespace {
void RetireAdapterLocked(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) {
  adapter->state = kernel::KGraphicsAdapter::State::Terminated;
  KE_OB_GetGfxDirectory()->Each<kernel::KDisplay>([&](const auto &target) {
    if (target->adapter != adapter || target->removed) return;
    target->isPresent = false;
    target->removed = true;
    target->isAttachedToDesktop = false;
    target->topologyDegraded = false;
    InvalidateActiveTarget(target);
    ++g_topology_generation;
    if (!g_topology_generation) ++g_topology_generation;
  });
}
}

void kernel::gfx::remove_device(const KDevicePtr<KDevice> &base) noexcept {
  user::Guard scene;
  user::SceneDrawGuard drawing;
  {
    DriverLock drivers;
    TopologyLock lock;
    // Drain USER drawing and exclude transactions before marking any stack
    // member removed. Queries never perform desktop retirement themselves.
    KE_OB_GetGfxDirectory()->Each<KGraphicsAdapter>([&](const auto &adapter) {
      for (auto current = base; current; current = current->attachedDevice)
        if (adapter->device == current) {
          RetireAdapterLocked(adapter);
          break;
        }
    });
    for (auto current = base; current; current = current->attachedDevice)
      current->pnpState = KPnpState::Removed;
  }
  user::cursor_reset_locked();
  const auto active = active_video_present_target();
  user::display_changed_locked(active ? active->currentMode.bpp : 0);
}

UNDOS_KERNEL_API_DEF void KE_GFX_RemoveAdapter(
    const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) noexcept {
  if (!adapter) return;
  kernel::user::Guard scene;
  kernel::user::SceneDrawGuard drawing;
  {
    DriverLock drivers;
    TopologyLock lock;
    RetireAdapterLocked(adapter);
  }
  kernel::user::cursor_reset_locked();
  const auto active = kernel::gfx::active_video_present_target();
  kernel::user::display_changed_locked(active ? active->currentMode.bpp : 0);
}

UNDOS_KERNEL_API_DEF kernel::KGraphicsAdapterResult KE_GFX_RegisterGraphicsAdapter(
    const kernel::KObjectPtr<kernel::KDriverObject> &driver,
    const kernel::KDevicePtr<kernel::KDevice> &backingDevice,
    const kstd::string_view &name) noexcept {
  auto created = kernel::CreateKObject<kernel::KGraphicsAdapter>(name);
  if (!created) {
    return kstd::unexpected(created.error());
  }
  auto ret = kstd::move(*created);
  ret->driver = driver;
  ret->device = backingDevice;
  ret->state = kernel::KGraphicsAdapter::State::Uninitialized;

  if (const kstd::error_code inserted = KE_OB_InsertObject(KE_OB_GetGfxDirectory(), ret);
      inserted) {
    return kstd::unexpected(inserted);
  }

  // Register interest in this device's state changes. The token lives on the
  // adapter, so the interest is unregistered when the adapter is destroyed.
  auto interest = KE_EVENT_RegisterInterest(
      kernel::event::Source(backingDevice),
      kernel::event::RegistrationContext{.fn = &OnBackingDeviceEvent, .user_buffer = {}});
  if (!interest) {
    (void) KE_OB_RemoveObject(KE_OB_GetGfxDirectory(), ret);
    return kstd::unexpected(interest.error());
  }
  ret->deviceStateInterest = kstd::move(*interest);

  return ret;
}

namespace {
kstd::error_code ProgramDisplayMode(
    const kernel::KObjectPtr<kernel::KDisplay> &display,
    const kernel::KObjectPtr<kernel::KSurface> &preparedSurface,
    size_t modeIndex, kernel::KDisplayModeReservation &reservation,
    kernel::gfx::ModeReservationGuard &reservationGuard,
    bool &hardwareAttempted, uint32_t &outcome,
    kernel::KDisplayModeInfo &programmedMode,
    kernel::KObjectPtr<kernel::KSurface> &programmedSurface) noexcept {
  hardwareAttempted = false;
  outcome = 0;
  if (!display || !display->ProgramMode || !display->FinishMode ||
      !display->QueryActualState)
    return kernel::KernelError::Unsupported;

  auto finish = [&](bool committed) { reservationGuard.Finish(committed); };
  // The generation-pinned selected mode was compared with this reservation
  // before entering the scene/drawing critical section.
  if (reservation.modeIndex != modeIndex ||
      !kernel::gfx::usable_mode(display, reservation.mode) ||
      !reservation.binding.backing ||
      reservation.binding.backing->size <
          static_cast<size_t>(reservation.mode.pitch) *
              reservation.mode.height) {
    return kernel::KernelError::InvalidState;
  }
  reservation.mode.modeIndex = modeIndex;
  const auto before = display->QueryActualState(display);
  if (!before) {
    return before.error();
  }

  auto surface = display->surface;
  bool insertedSurface = false;
  if (!surface) {
    surface = preparedSurface;
    if (const kstd::error_code inserted = KE_OB_InsertObject(KE_OB_GetGfxDirectory(), surface);
        inserted) {
      KE_Print("GFX: failed to register target surface object: {}.", inserted.message());
      return inserted;
    }
    insertedSurface = true;
  }

  hardwareAttempted = true;
  if (const auto disabled = display->Disable(display); disabled) {
    finish(false);
    if (insertedSurface) (void) KE_OB_RemoveObject(KE_OB_GetGfxDirectory(), surface);
    const auto actual = display->QueryActualState(display);
    display->topologyDegraded = !actual || !actual->known;
    display->isAttachedToDesktop = actual && actual->known && actual->enabled;
    outcome = display->topologyDegraded ? 3u : 1u;
    return {};
  }
  const auto programError = display->ProgramMode(display, reservation);
  const auto restorePrevious = [&]() {
    if (!before->known) return false;
    if (before->modeIndex != modeIndex) {
      if (!display->RestoreMode ||
          display->RestoreMode(display, before->modeIndex, reservation))
        return false;
    }
    if (before->enabled && display->Enable && display->Enable(display)) return false;
    if (!before->enabled && display->Disable && display->Disable(display)) return false;
    return true;
  };

  if (programError) {
    const bool restored = restorePrevious();
    finish(false);
    if (insertedSurface) (void) KE_OB_RemoveObject(KE_OB_GetGfxDirectory(), surface);
    const auto actual = display->QueryActualState(display);
    display->topologyDegraded = !restored || !actual || !actual->known ||
                                actual->enabled != before->enabled ||
                                (actual->known && before->known &&
                                 actual->modeIndex != before->modeIndex);
    display->isAttachedToDesktop = actual && actual->known && actual->enabled;
    if (display->topologyDegraded) outcome = 3;
    else outcome = 1;
    if (!display->isAttachedToDesktop) ++display->modeGeneration;
    KE_Print("GFX: target {} failed while programming mode {}: {}.",
             display->name, modeIndex, programError.message());
    return {};
  }

  const auto enabled = display->Enable ? display->Enable(display)
                                       : kernel::KernelError::Success;
  if (enabled) {
    const bool restored = restorePrevious();
    finish(false);
    if (insertedSurface) (void) KE_OB_RemoveObject(KE_OB_GetGfxDirectory(), surface);
    const auto actual = display->QueryActualState(display);
    display->topologyDegraded = !restored || !actual || !actual->known ||
                                actual->enabled != before->enabled ||
                                (actual->known && before->known &&
                                 actual->modeIndex != before->modeIndex);
    display->isAttachedToDesktop = actual && actual->known && actual->enabled;
    outcome = display->topologyDegraded ? 3u : 2u;
    if (!display->isAttachedToDesktop) ++display->modeGeneration;
    KE_Print("GFX: target {} programmed mode but failed to enable output: {}.",
             display->name, enabled.message());
    return {};
  }

  const auto &binding = reservation.binding;
  display->surface = surface;
  surface->display = display;
  surface->backing = binding.backing;
  surface->type = binding.type;
  surface->flags = binding.flags;
  surface->width = reservation.mode.width;
  surface->height = reservation.mode.height;
  surface->pitch = static_cast<int32_t>(reservation.mode.pitch);
  surface->bpp = reservation.mode.bpp;
  surface->pixelFormat = reservation.mode.pixelFormat;
  surface->sizeInBytes =
      static_cast<size_t>(reservation.mode.pitch) * reservation.mode.height;
  display->currentMode = reservation.mode;
  display->isAttachedToDesktop = true;
  display->topologyDegraded = false;
  ++display->modeGeneration;
  programmedMode = reservation.mode;
  programmedSurface = surface;
  finish(true);
  return {};
}

}// namespace

namespace kernel::gfx {
kstd::expected<VideoPresentTargetResult, kstd::error_code>
commit_video_present_path(uint32_t generation, const VideoPresentPathInfo &path,
                          const KObjectPtr<KDisplay> &expectedTarget,
                          KDisplayModeReservation &reservation,
                          size_t resultCapacity, bool *firstActivation) noexcept {
  const auto snapshot = acquire_video_present_snapshot(generation);
  if (!snapshot) {
    if (snapshot.error() == KernelError::Retry)
      video_present_release_reservation(expectedTarget, reservation);
    return kstd::unexpected(snapshot.error());
  }
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
  if (g_before_commit_decision) {
    const auto hook = g_before_commit_decision;
    g_before_commit_decision = nullptr;
    hook();
  }
#endif
  if (!resultCapacity) {
    DriverLock drivers;
    ModeReservationGuard reservationGuard(expectedTarget, reservation);
    {
      TopologyLock lock;
      if (const auto error = CheckGeneration(generation); error)
        return kstd::unexpected(error);
      // This is the serialized precedence decision. A later invalidation
      // follows this current-generation capacity failure.
      reservationGuard.finished = true;
    }
    return kstd::unexpected(KernelError::BufferTooSmall);
  }
  const auto replacement = CloneSnapshot(**snapshot);
  if (!replacement) return kstd::unexpected(replacement.error());
  const auto surfaceId = KE_OB_MakeUniqueName(KE_OB_GetGfxDirectory(), "Surface");
  if (!surfaceId) return kstd::unexpected(surfaceId.error());
  kstd::static_string<64> surfaceName;
  kstd::format(surfaceName, "Surface{}", *surfaceId);
  const auto preparedSurface = CreateKObject<KSurface>(surfaceName);
  if (!preparedSurface) return kstd::unexpected(preparedSurface.error());
  VideoPresentSnapshotLease retired;
  user::Guard scene;
  user::SceneDrawGuard drawing;
  DriverLock drivers;
  ModeReservationGuard reservationGuard(expectedTarget, reservation);
  {
    TopologyLock lock;
    if (const auto error = CheckGeneration(generation); error)
      return kstd::unexpected(error);
  }
  if (firstActivation) *firstActivation = !ActiveTargetUnderGate();
  if (!g_desktop_source || path.sourceId != g_desktop_source->sourceId ||
      path.sourceX != 0 || path.sourceY != 0 ||
      path.flags != 0 || !path.width || !path.height)
    return kstd::unexpected(KernelError::InvalidArgument);

  const auto target = FindTarget(path.targetId);
  if (!target || !target->isPresent || target != expectedTarget ||
      !target->Validate)
    return kstd::unexpected(KernelError::NotFound);
  if (const auto error = target->Validate(target); error)
    return kstd::unexpected(error);
  const VideoPresentSourceModeInfo *sourceMode = nullptr;
  for (size_t index = 0; index < (*snapshot)->topology.sourceModeCount; ++index)
    if ((*snapshot)->sourceModes[index].modeId == path.sourceModeId) sourceMode = &(*snapshot)->sourceModes[index];
  if (!sourceMode || sourceMode->width != path.width || sourceMode->height != path.height)
    return kstd::unexpected(KernelError::InvalidArgument);

  const VideoPresentTargetModeInfo *selected = nullptr;
  for (size_t index = 0; index < (*snapshot)->topology.targetModeCount; ++index)
    if ((*snapshot)->targetModes[index].targetId == path.targetId &&
        (*snapshot)->targetModes[index].modeId == path.targetModeId)
      selected = &(*snapshot)->targetModes[index];
  if (!selected || selected->mode.width != path.width ||
      selected->mode.height != path.height ||
      selected->mode.bpp != sourceMode->colorDepth ||
      selected->mode.pixelFormat != sourceMode->pixelFormat)
    return kstd::unexpected(KernelError::Unsupported);
  if (reservation.modeIndex != selected->mode.modeIndex ||
      reservation.mode.width != selected->mode.width ||
      reservation.mode.height != selected->mode.height ||
      reservation.mode.pitch != selected->mode.pitch ||
      reservation.mode.bpp != selected->mode.bpp ||
      reservation.mode.refreshRate != selected->mode.refreshRate ||
      reservation.mode.pixelFormat != selected->mode.pixelFormat ||
      !reservation.binding.backing)
    return kstd::unexpected(KernelError::InvalidState);
  if (g_desktop_source && g_desktop_source->active &&
      g_desktop_source->target == target &&
      target->isAttachedToDesktop &&
      target->currentMode.modeIndex == selected->mode.modeIndex)
    return VideoPresentTargetResult{
        .targetId = target->videoPresentTargetId,
        .requestedModeId = path.targetModeId,
        .actualModeId = path.targetModeId,
        .active = 1,
        .outcome = 0,
        .degraded = target->topologyDegraded ? 1u : 0u};

  const auto publish = [&]() {
    auto &next = const_cast<VideoPresentSnapshot &>(**replacement);
    for (uint32_t i = 0; i < next.topology.targetCount; ++i)
      Observe(next.targetReferences[i], next.observations[i]);
    TopologyLock lock;
    ++g_topology_generation;
    if (!g_topology_generation) ++g_topology_generation;
    next.topology.generation = g_topology_generation;
    CapturePaths(next);
    retired = VideoPresentSnapshotLease(g_snapshot);
    next.references.fetch_add(1);
    g_snapshot = &next;
  };
  bool attempted = false;
  uint32_t outcome = 0;
  KDisplayModeInfo programmedMode{};
  KObjectPtr<KSurface> programmedSurface;
  const auto error = ProgramDisplayMode(target, *preparedSurface, selected->mode.modeIndex,
                                        reservation, reservationGuard, attempted,
                                        outcome, programmedMode, programmedSurface);
  if (!attempted) {
    if (error == KernelError::OutOfMemory) reservationGuard.finished = true;
    return kstd::unexpected(error);
  }

  if (outcome == 0) {
    if (g_desktop_source && g_desktop_source->active &&
        g_desktop_source->target && g_desktop_source->target != target) {
      const auto previous = g_desktop_source->target;
      const auto disableError = previous->Disable
          ? previous->Disable(previous)
          : KernelError::Unsupported;
      const auto previousState = previous->QueryActualState(previous);
      if (disableError || !previousState || !previousState->known ||
          previousState->enabled) {
        if (target->Disable) (void) target->Disable(target);
        const auto previousActual = previous->QueryActualState
            ? previous->QueryActualState(previous)
            : kstd::expected<KDisplayActualState, kstd::error_code>(
                  kstd::unexpected(KernelError::Unsupported));
        const auto targetActual = target->QueryActualState(target);
        previous->isAttachedToDesktop =
            previousActual && previousActual->known && previousActual->enabled;
        target->isAttachedToDesktop =
            targetActual && targetActual->known && targetActual->enabled;
        previous->topologyDegraded = true;
        target->topologyDegraded = true;
        outcome = 3;
        if (previous->isAttachedToDesktop && !target->isAttachedToDesktop) {
          g_desktop_source->target = previous;
        } else if (target->isAttachedToDesktop && !previous->isAttachedToDesktop) {
          g_desktop_source->target = target;
          g_desktop_source->surface = target->surface;
          g_desktop_source->currentMode = target->currentMode;
          g_active_path = path;
          g_active_path_valid = true;
        } else {
          g_desktop_source->target = nullptr;
          g_desktop_source->surface = nullptr;
          g_desktop_source->currentMode = {};
          g_desktop_source->active = false;
          g_active_path_valid = false;
        }
        g_desktop_source->active = g_desktop_source->target != nullptr;
        publish();
        return VideoPresentTargetResult{
            .targetId = target->videoPresentTargetId,
            .requestedModeId = path.targetModeId,
            .actualModeId = targetActual && targetActual->known &&
                                    targetActual->modeIndex < UINT32_MAX
                                ? ModeIdFromIndex(targetActual->modeIndex)
                                : 0,
            .active = !targetActual || !targetActual->known
                          ? 2u
                          : (targetActual->enabled ? 1u : 0u),
            .outcome = outcome,
            .degraded = 1};
      }
      previous->isAttachedToDesktop = false;
    }
    g_desktop_source->target = target;
    g_desktop_source->surface = programmedSurface;
    g_desktop_source->currentMode = programmedMode;
    g_desktop_source->active = true;
    g_active_path = path;
    g_active_path_valid = true;
  } else if (g_desktop_source && g_desktop_source->active &&
             g_desktop_source->target == target &&
             !target->isAttachedToDesktop) {
    g_desktop_source->target = nullptr;
    g_desktop_source->surface = nullptr;
    g_desktop_source->currentMode = {};
    g_desktop_source->active = false;
    g_active_path_valid = false;
  }
  publish();

  const auto actual = target->QueryActualState
      ? target->QueryActualState(target)
      : kstd::expected<KDisplayActualState, kstd::error_code>(
            kstd::unexpected(KernelError::Unsupported));
  const uint32_t actualModeId = actual && actual->known &&
                                        actual->modeIndex < UINT32_MAX
                                    ? ModeIdFromIndex(actual->modeIndex)
                                    : 0;
  return VideoPresentTargetResult{
      .targetId = target->videoPresentTargetId,
      .requestedModeId = path.targetModeId,
      .actualModeId = actualModeId,
      .active = !actual || !actual->known ? 2u : (actual->enabled ? 1u : 0u),
      .outcome = outcome,
      .degraded = target->topologyDegraded ? 1u : 0u};
}
}// namespace kernel::gfx
