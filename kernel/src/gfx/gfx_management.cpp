
#include "stdkrn.hpp"

namespace {
// First KDisplay ever registered via KE_GFX_RegisterDisplay; this is the
// display GDI-style syscalls target when no specific device is requested.
kernel::KObjectPtr<kernel::KDisplay> g_primary_display;

void StartAdapter(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter) {
  KE_Print("GFX: Adapter {} started", adapter->name);

  adapter->state = kernel::KGraphicsAdapter::State::Probing;
  if (adapter->Probe && !adapter->Probe(adapter)) {
    KE_Print("GFX: Adapter {} probe failed", adapter->name);
    adapter->state = kernel::KGraphicsAdapter::State::Terminated;
    return;
  }
  adapter->state = kernel::KGraphicsAdapter::State::NeedsDisplayEnumerated;

  if (adapter->EnumerateDisplays && !adapter->EnumerateDisplays(adapter)) {
    KE_Print("GFX: Adapter {} display enumeration failed", adapter->name);
    adapter->state = kernel::KGraphicsAdapter::State::Terminated;
    return;
  }

  adapter->state = kernel::KGraphicsAdapter::State::Running;

  // TODO: Bring up the display(s) with defaults ?
}

// Fired on the event pump thread whenever the adapter's backing device raises
// an event. Richer handling (re-probe / tear down displays on removal) will be
// added as the PnP state machine grows; for now this just traces.
void OnBackingDeviceEvent(const kernel::event::Source &source,
                          const kernel::event::Code &code,
                          const kernel::DataBuffer &userBuffer) {
  (void) userBuffer;

  if (source.type == kernel::event::Source::Type::KObject) {
    if (const auto gfx = source.kobject.As<kernel::KGraphicsAdapter>()) {
      if (gfx->state == kernel::KGraphicsAdapter::State::Uninitialized && code.is(KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Started)) {
        StartAdapter(gfx);
        return;
      }
    }
  }
}
}// namespace

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

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDisplay> KE_GFX_RegisterDisplay(
    const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
    const kstd::string_view &name) noexcept {
  const auto next_display_id = KE_OB_MakeUniqueName(KE_OB_GetGfxDirectory(), name);

  // Make unique name
  kstd::static_string<64> realName;
  kstd::format(realName, "{}{}", name, next_display_id);
  auto ret = kernel::CreateKObject<kernel::KDisplay>(realName);
  ret->adapter = adapter;

  if (!KE_OB_InsertObject(KE_OB_GetGfxDirectory(), ret)) {
    return nullptr;
  }

  if (!g_primary_display) {
    g_primary_display = ret;
  }

  return ret;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDisplay> KE_GFX_GetPrimaryDisplay() noexcept {
  return g_primary_display;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KSurface> KE_GFX_CreateSurfaceForMode(
    const kernel::KObjectPtr<kernel::KDisplay> &display,
    size_t mode) noexcept {
  if (!display || !display->adapter) {
    return nullptr;
  }

  if (!display->adapter->SetMode) {
    KE_Print("KE_GFX_CreateSurfaceForMode: No driver bound to actually program a mode: nothing we can do.");
    return nullptr;
  }

  if (display->GetModeCount && mode >= display->GetModeCount(display)) {
    KE_Print("KE_GFX_CreateSurfaceForMode: Out-of-range mode index: don't even ask the driver about it.");
    return nullptr;
  }

  const auto next_surface_id = KE_OB_MakeUniqueName(KE_OB_GetGfxDirectory(), "Surface");
  kstd::static_string<64> name;
  kstd::format(name, "Surface{}", next_surface_id);
  auto ret = kernel::CreateKObject<kernel::KSurface>(name);
  if (!ret) {
    KE_Print("KE_GFX_CreateSurfaceForMode: Failed to CreateKObject surface object.");
    return nullptr;
  }

  if (!display->adapter->SetMode(display->adapter, display, ret, mode)) {
    KE_Print("KE_GFX_CreateSurfaceForMode: Failed to set mode.");
    return nullptr;
  }

  // Populate the surface's shape from the display's mode description so
  // callers (and drivers) don't need to fill these in themselves.
  if (kernel::KDisplayModeInfo modeInfo; display->GetModeInfo && display->GetModeInfo(display, mode, modeInfo)) {
    ret->width = modeInfo.width;
    ret->height = modeInfo.height;
    ret->pitch = modeInfo.pitch;
    ret->pixelFormat = modeInfo.pixelFormat;
  }

  if (!KE_OB_InsertObject(KE_OB_GetGfxDirectory(), ret)) {
    display->adapter->UnsetMode(display->adapter, display);
    KE_Print("KE_GFX_CreateSurfaceForMode: Failed to KE_OB_InsertObject surface object.");
    return nullptr;
  }

  return ret;
}
