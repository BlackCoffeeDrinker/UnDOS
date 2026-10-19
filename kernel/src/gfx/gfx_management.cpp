
#include "stdkrn.hpp"

#include "gfx/gfx.hpp"

namespace {
// First KDisplay the graphics subsystem created while probing adapters; this
// is the display GDI-style syscalls target when no specific device is
// requested. Election happens in StartAdapter, the sole creator of displays.
kernel::KObjectPtr<kernel::KDisplay> g_primary_display;

// Walks the driver's mode list, skipping anything the monitor cannot display,
// and invokes `func(modeIndex, modeInfo)` for each survivor. Enumeration stops
// at the first index the driver refuses.
template<typename Func>
void EachSupportedMode(const kernel::KObjectPtr<kernel::KDisplay> &display, Func &&func) {
  if (!display || !display->GetModeInfo) {
    return;
  }

  for (size_t modeIndex = 0;; ++modeIndex) {
    kernel::KDisplayModeInfo modeInfo{};
    if (!display->GetModeInfo(display, modeIndex, modeInfo)) {
      break;
    }

    if (!kernel::gfx::usable_mode(display, modeInfo)) {
      KE_Print("GFX: Display {} mode {} ({}x{}x{}@{}) rejected by the monitor capabilities",
               display->name, modeIndex, modeInfo.width, modeInfo.height, modeInfo.bpp, modeInfo.refreshRate);
      continue;
    }

    func(modeIndex, modeInfo);
  }
}

void StartAdapter(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) {
  KE_Print("GFX: Adapter {} started", adapter->name);

  adapter->state = kernel::KGraphicsAdapter::State::Probing;
  if (adapter->Probe && !adapter->Probe(adapter)) {
    KE_Print("GFX: Adapter {} probe failed", adapter->name);
    adapter->state = kernel::KGraphicsAdapter::State::Terminated;
    return;
  }
  adapter->state = kernel::KGraphicsAdapter::State::NeedsDisplayEnumerated;

  KE_Print("GFX: Adapter {} display enumeration started", adapter->name);
  if (!adapter->ProbeDisplay) {
    KE_Print("GFX: Adapter {} display enumeration failed - method not present", adapter->name);
    adapter->state = kernel::KGraphicsAdapter::State::Terminated;
    return;
  }

  // Cap head enumeration at a reasonable upper bound (e.g., 16 physical heads per adapter)
  constexpr uint32_t MaxDisplayHeads = 16;

  for (uint32_t displayIndex = 0; displayIndex < MaxDisplayHeads; displayIndex++) {
    kernel::KGraphicsAdapterDisplayEnumInfo displayInfo{};
    displayInfo.displayIndex = displayIndex;

    kernel::KGraphicsAdapterDisplayInfo outInfo{};
    if (!adapter->ProbeDisplay(adapter, displayInfo, outInfo)) {
      break;// Driver reported no more display heads
    }

    if (!outInfo.isPresent) {
      continue;// Connector exists on hardware but no display attached
    }

    // Parse out the EDID if there's one so we have a fallback name
    if (outInfo.edidSize > kernel::MaxEdidSize) {
      outInfo.edidSize = kernel::MaxEdidSize;
    }

    kernel::Edid edid{};
    if (outInfo.isPnP && kernel::Edid::ValidateHeader(kstd::span(outInfo.edid.data(), outInfo.edidSize))) {
      edid.Assign(kstd::span(outInfo.edid.data(), outInfo.edidSize));
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
      kstd::format(finalName, "{}{}", baseName, KE_OB_MakeUniqueName(KE_OB_GetGfxDirectory(), baseName));
    } else {
      finalName = baseName;
    }

    // Instantiate and populate KDisplay object
    auto display = kernel::CreateKObject<kernel::KDisplay>(finalName);
    display->adapter = adapter;
    display->headIndex = displayIndex;
    display->isPresent = outInfo.isPresent;
    display->isMonitor = outInfo.isMonitor;
    display->edid = edid;

    // Let the driver bind its per-head entry points on the display we just
    // created. This is the only way a driver influences a KDisplay.
    if (!adapter->ConfigureDisplay) {
      KE_Print("GFX: Adapter {} has no ConfigureDisplay; display {} will be unusable", adapter->name, display->name);
    } else if (!adapter->ConfigureDisplay(adapter, display)) {
      KE_Print("GFX: Adapter {} discarded display head {}", adapter->name, displayIndex);
      continue;
    } else if (!display->GetModeInfo || !display->SetMode) {
      KE_Print("GFX: Adapter {} left display {} without GetModeInfo/SetMode", adapter->name, display->name);
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

    if (!KE_OB_InsertObject(KE_OB_GetGfxDirectory(), display)) {
      KE_Print("GFX: Failed to register display {} for adapter {}", display->name, adapter->name);
      continue;
    }

    // Designate primary display if none exists yet
    if (!g_primary_display) {
      g_primary_display = display;
      display->isPrimary = true;
      KE_Print("GFX: Display {} set as primary display", display->name);
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

// Fired on the event pump thread whenever the adapter's backing device raises
// an event. Richer handling (re-probe / tear down displays on removal) will be
// added as the PnP state machine grows; for now this just traces.
void OnBackingDeviceEvent(const kernel::event::Source &source,
                          const kernel::event::Code &code,
                          const kernel::DataBuffer &userBuffer) {
  (void) userBuffer;

  if (const auto gfx = source.As<kernel::KDevice>()) {
    if (code.is(KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Started)) {
      // Need to find the adapter for this device.
      FindAdaptersForDevice(gfx, &StartAdapter);
    }
  }
}
}// namespace

namespace kernel::gfx {
bool usable_mode(const KObjectPtr<KDisplay> &display, const KDisplayModeInfo &mode) noexcept {
  return display && mode.width && mode.height && mode.width <= 32767 && mode.height <= 32767 &&
         mode.pitch && mode.pitch <= INT32_MAX &&
         static_cast<uint64_t>(mode.pitch) * mode.height <= SIZE_MAX &&
         display->monitorCaps.Supports(mode);
}

bool enumerate_mode(const KObjectPtr<KDisplay> &display, uint32_t ordinal, KDisplayModeInfo &out) noexcept {
  out = {};
  bool found = false;
  EachSupportedMode(display, [&](size_t index, const KDisplayModeInfo &info) {
    if (found) return;
    if (ordinal) {
      --ordinal;
    } else {
      out = info;
      out.modeIndex = index;
      found = true;
    }
  });
  return found;
}

void start_adapter(const KObjectPtr<KGraphicsAdapter> &adapter) noexcept {
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

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KGraphicsAdapter> KE_GFX_RegisterGraphicsAdapter(
    const kernel::KObjectPtr<kernel::KDriverObject> &driver,
    const kernel::KDevicePtr<kernel::KDevice> &backingDevice,
    const kstd::string_view &name) noexcept {
  auto ret = kernel::CreateKObject<kernel::KGraphicsAdapter>(name);
  ret->driver = driver;
  ret->device = backingDevice;
  ret->state = kernel::KGraphicsAdapter::State::Uninitialized;

  if (!KE_OB_InsertObject(KE_OB_GetGfxDirectory(), ret)) {
    return nullptr;
  }

  // Register interest in this device's state changes. The token lives on the
  // adapter, so the interest is unregistered when the adapter is destroyed.
  ret->deviceStateInterest = KE_EVENT_RegisterInterest(
      kernel::event::Source(backingDevice),
      kernel::event::RegistrationContext{.fn = &OnBackingDeviceEvent, .user_buffer = {}});

  return ret;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDisplay> KE_GFX_GetPrimaryDisplay() noexcept {
  return g_primary_display;
}

UNDOS_KERNEL_API_DEF bool KE_GFX_SetDisplayMode(
    const kernel::KObjectPtr<kernel::KDisplay> &display,
    size_t modeIndex) noexcept {
  if (!display) {
    return false;
  }

  if (!display->GetModeInfo || !display->SetMode) {
    KE_Print("KE_GFX_SetDisplayMode: Display {} has no mode programming hooks.", display->name);
    return false;
  }

  // Range check: the driver reports false once modeIndex runs past its list.
  kernel::KDisplayModeInfo modeInfo{};
  if (!display->GetModeInfo(display, modeIndex, modeInfo)) {
    KE_Print("KE_GFX_SetDisplayMode: Display {} does not have a mode {}.", display->name, modeIndex);
    return false;
  }

  // The monitor, not the adapter, has the last word on what may be programmed.
  if (!kernel::gfx::usable_mode(display, modeInfo)) {
    KE_Print("KE_GFX_SetDisplayMode: Display {} mode {} ({}x{}x{}@{}) has unsupported geometry or monitor timing.",
             display->name, modeIndex, modeInfo.width, modeInfo.height, modeInfo.bpp, modeInfo.refreshRate);
    return false;
  }

  modeInfo.modeIndex = modeIndex;

  auto surface = display->surface;
  const bool newSurface = !surface;
  if (newSurface) {
    const auto next_surface_id = KE_OB_MakeUniqueName(KE_OB_GetGfxDirectory(), "Surface");
    kstd::static_string<64> name;
    kstd::format(name, "Surface{}", next_surface_id);
    surface = kernel::CreateKObject<kernel::KSurface>(name);
    if (!surface || !KE_OB_InsertObject(KE_OB_GetGfxDirectory(), surface)) {
      KE_Print("KE_GFX_SetDisplayMode: Failed to prepare surface object.");
      return false;
    }
  }

  if (display->Disable) {
    display->Disable(display);
  }

  kernel::KDisplaySurfaceBinding binding{};
  if (!display->SetMode(display, modeIndex, binding)) {
    KE_Print("KE_GFX_SetDisplayMode: Display {} refused mode {}.", display->name, modeIndex);
    // Bring the previously programmed mode back up rather than leaving the
    // screen blanked.
    if (newSurface && !KE_OB_RemoveObject(KE_OB_GetGfxDirectory(), surface)) {
      KE_Print("KE_GFX_SetDisplayMode: Failed to remove unused surface.");
    }
    if (display->isAttachedToDesktop && display->Enable && !display->Enable(display)) {
      display->isAttachedToDesktop = false;
      ++display->modeGeneration;
      KE_Print("KE_GFX_SetDisplayMode: Failed to re-enable previous output.");
    }
    return false;
  }

  // The surface object identity is kept stable across mode changes so device
  // contexts already holding it stay valid; only its fields are rewritten.
  display->surface = surface;
  surface->display = display;
  surface->backing = binding.backing;
  surface->type = binding.type;
  surface->flags = binding.flags;

  surface->width = modeInfo.width;
  surface->height = modeInfo.height;
  surface->pitch = static_cast<int32_t>(modeInfo.pitch);
  surface->bpp = modeInfo.bpp;
  surface->pixelFormat = modeInfo.pixelFormat;
  surface->sizeInBytes = static_cast<size_t>(modeInfo.pitch) * modeInfo.height;

  display->currentMode = modeInfo;
  ++display->modeGeneration;
  display->isAttachedToDesktop = !display->Enable || display->Enable(display);
  if (!display->isAttachedToDesktop) {
    KE_Print("KE_GFX_SetDisplayMode: Display {} failed to enable output.", display->name);
    return false;
  }

  return true;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KSurface> KE_GFX_CreateSurfaceForMode(
    const kernel::KObjectPtr<kernel::KDisplay> &display,
    size_t mode) noexcept {
  if (!KE_GFX_SetDisplayMode(display, mode)) {
    return nullptr;
  }

  return display->surface;
}
