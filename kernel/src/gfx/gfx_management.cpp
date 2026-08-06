
#include "stdkrn.hpp"

namespace {
uint32_t g_next_surface_id = 0;
uint32_t g_next_display_id = 0;

// First KDisplay ever registered via KE_GFX_RegisterDisplay; this is the
// display GDI-style syscalls target when no specific device is requested.
kernel::KObjectPtr<kernel::KDisplay> g_primary_display;
}// namespace

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KGraphicsAdapter> KE_GFX_RegisterGraphicsAdapter(
    const kernel::KObjectPtr<kernel::KDriverObject> &driver,
    const kernel::KDevicePtr<kernel::KDevice> &backingDevice,
    const kstd::string_view &name) noexcept {
  auto ret = kernel::CreateKObject<kernel::KGraphicsAdapter>(name);
  ret->driver = driver;
  ret->device = backingDevice;

  if (!KE_OB_InsertObject(KE_OB_GetGfxDirectory(), ret)) {
    return nullptr;
  }

  return ret;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDisplay> KE_GFX_RegisterDisplay(
    const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
    const kstd::string_view &name) noexcept {

  kstd::static_string<64> realName;

  // Make unique name
  kstd::format(realName, "{}{}", name, g_next_display_id++);
  auto ret = kernel::CreateKObject<kernel::KDisplay>(realName);
  ret->adapter = adapter;

  if (!KE_OB_InsertObject(KE_OB_GetGfxDirectory(), ret)) {
    g_next_display_id--;
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
    // No driver bound to actually program a mode: nothing we can do.
    return nullptr;
  }

  if (display->GetModeCount && mode >= display->GetModeCount()) {
    // Out-of-range mode index: don't even ask the driver about it.
    return nullptr;
  }

  kstd::static_string<64> name;
  kstd::format(name, "Surface{}", g_next_surface_id++);
  auto ret = kernel::CreateKObject<kernel::KSurface>(name);
  if (!ret) {
    return nullptr;
  }

  if (!display->adapter->SetMode(display, ret, mode)) {
    // ERROR OUT
    return nullptr;
  }

  // Populate the surface's shape from the display's mode description so
  // callers (and drivers) don't need to fill these in themselves.
  kernel::KDisplayModeInfo modeInfo;
  if (display->GetModeInfo && display->GetModeInfo(mode, modeInfo)) {
    ret->width = modeInfo.width;
    ret->height = modeInfo.height;
    ret->pitch = modeInfo.pitch;
    ret->pixelFormat = modeInfo.pixelFormat;
  }

  if (!KE_OB_InsertObject(KE_OB_GetGfxDirectory(), ret)) {
    display->adapter->UnsetMode(display);
    return nullptr;
  }

  return ret;
}
