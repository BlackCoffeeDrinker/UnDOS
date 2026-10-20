
#include "stdkrn.hpp"

#include "gfx/gfx.hpp"

namespace {
// First KDisplay the graphics subsystem created while probing adapters; this
// is the display GDI-style syscalls target when no specific device is
// requested. Election happens in StartAdapter, the sole creator of displays.
kernel::KObjectPtr<kernel::KDisplay> g_primary_display;

void StartAdapter(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) {
  KE_Print("GFX: Adapter {} started", adapter->name);

  adapter->state = kernel::KGraphicsAdapter::State::Probing;
  if (const kstd::error_code probed = adapter->Probe ? adapter->Probe(adapter) : kernel::KernelError::Success;
      probed) {
    KE_Print("GFX: Adapter {} probe failed: {}", adapter->name, probed.message());
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
    if (const kstd::error_code probed = adapter->ProbeDisplay(adapter, displayInfo, outInfo);
        probed) {
      if (probed != kernel::KernelError::OutOfRange) {
        KE_Print("GFX: Adapter {} display head {} probe failed: {}", adapter->name, displayIndex, probed.message());
      }
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

    if (const kstd::error_code inserted = KE_OB_InsertObject(KE_OB_GetGfxDirectory(), display);
        inserted) {
      KE_Print("GFX: Failed to register display {} for adapter {}: {}", display->name, adapter->name, inserted.message());
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

kstd::expected<KDisplayModeInfo, kstd::error_code> enumerate_mode(const KObjectPtr<KDisplay> &display, uint32_t ordinal) noexcept {
  if (!display) return kstd::unexpected(KernelError::InvalidArgument);
  if (!display->GetModeInfo) return kstd::unexpected(KernelError::Unsupported);
  for (size_t index = 0;; ++index) {
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

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDisplay> KE_GFX_GetPrimaryDisplay() noexcept {
  return g_primary_display;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_GFX_SetDisplayMode(
    const kernel::KObjectPtr<kernel::KDisplay> &display,
    size_t modeIndex) noexcept {
  if (!display) {
    return kernel::KernelError::InvalidArgument;
  }

  if (!display->GetModeInfo || !display->SetMode) {
    KE_Print("KE_GFX_SetDisplayMode: Display {} has no mode programming hooks.", display->name);
    return kernel::KernelError::Unsupported;
  }

  // Range check: the driver reports OutOfRange once modeIndex runs past its list.
  const auto queried = display->GetModeInfo(display, modeIndex);
  if (!queried) {
    KE_Print("KE_GFX_SetDisplayMode: Display {} does not have a mode {}: {}.", display->name, modeIndex, queried.error().message());
    return queried.error();
  }
  kernel::KDisplayModeInfo modeInfo = *queried;

  // The monitor, not the adapter, has the last word on what may be programmed.
  if (!kernel::gfx::usable_mode(display, modeInfo)) {
    KE_Print("KE_GFX_SetDisplayMode: Display {} mode {} ({}x{}x{}@{}) has unsupported geometry or monitor timing.",
             display->name, modeIndex, modeInfo.width, modeInfo.height, modeInfo.bpp, modeInfo.refreshRate);
    return kernel::KernelError::Unsupported;
  }

  modeInfo.modeIndex = modeIndex;

  auto surface = display->surface;
  const bool newSurface = !surface;
  if (newSurface) {
    const auto next_surface_id = KE_OB_MakeUniqueName(KE_OB_GetGfxDirectory(), "Surface");
    if (!next_surface_id) return next_surface_id.error();
    kstd::static_string<64> name;
    kstd::format(name, "Surface{}", *next_surface_id);
    auto created = kernel::CreateKObject<kernel::KSurface>(name);
    if (!created) {
      KE_Print("KE_GFX_SetDisplayMode: Failed to allocate surface object.");
      return created.error();
    }
    surface = kstd::move(*created);
    if (const kstd::error_code inserted = KE_OB_InsertObject(KE_OB_GetGfxDirectory(), surface);
        inserted) {
      KE_Print("KE_GFX_SetDisplayMode: Failed to register surface object: {}.", inserted.message());
      return inserted;
    }
  }

  if (display->Disable) {
    display->Disable(display);
  }

  const auto programmed = display->SetMode(display, modeIndex);
  if (!programmed) {
    KE_Print("KE_GFX_SetDisplayMode: Display {} refused mode {}: {}.", display->name, modeIndex, programmed.error().message());
    // Bring the previously programmed mode back up rather than leaving the
    // screen blanked.
    if (newSurface && KE_OB_RemoveObject(KE_OB_GetGfxDirectory(), surface)) {
      KE_Print("KE_GFX_SetDisplayMode: Failed to remove unused surface.");
    }
    if (display->isAttachedToDesktop && display->Enable && display->Enable(display)) {
      display->isAttachedToDesktop = false;
      ++display->modeGeneration;
      KE_Print("KE_GFX_SetDisplayMode: Failed to re-enable previous output.");
    }
    return programmed.error();
  }
  const kernel::KDisplaySurfaceBinding &binding = *programmed;

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
  const kstd::error_code enabled = display->Enable ? display->Enable(display) : kernel::KernelError::Success;
  display->isAttachedToDesktop = !enabled;
  if (!display->isAttachedToDesktop) {
    KE_Print("KE_GFX_SetDisplayMode: Display {} failed to enable output: {}.", display->name, enabled.message());
    return enabled;
  }

  return kernel::KernelError::Success;
}

UNDOS_KERNEL_API_DEF kernel::KSurfaceResult KE_GFX_CreateSurfaceForMode(
    const kernel::KObjectPtr<kernel::KDisplay> &display,
    size_t mode) noexcept {
  if (const kstd::error_code status = KE_GFX_SetDisplayMode(display, mode); status) {
    return kstd::unexpected(status);
  }

  return display->surface;
}
