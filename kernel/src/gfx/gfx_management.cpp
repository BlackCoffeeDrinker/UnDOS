
#include "stdkrn.hpp"

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KGraphicsAdapter> KE_GFX_RegisterGraphicsAdapter(
    const kernel::KObjectPtr<kernel::KDriverObject> &driver,
    const kernel::KDevicePtr<kernel::KDevice> &backingDevice,
    const kstd::string_view &name) noexcept {
  auto ret = kernel::CreateKObject<kernel::KGraphicsAdapter>(name);
  ret->driver = driver;
  ret->device = backingDevice;

  // TODO: Validate name is unique
  // TODO: Other validation ?
  // TODO: Register in the object manager

  return ret;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDisplay> KE_GFX_RegisterDisplay(
    const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
    const kstd::string_view &name) noexcept {
  auto ret = kernel::CreateKObject<kernel::KDisplay>(name);
  ret->adapter = adapter;
  // TODO: Validate name is unique
  // TODO: Other validation ?
  // TODO: Register in the object manager
  return ret;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KSurface> KE_GFX_CreateSurface(
    const kernel::KObjectPtr<kernel::KDisplay> &display,
    size_t mode) noexcept {
  auto ret = kernel::CreateKObject<kernel::KSurface>("TODO");
  if (!display->adapter->SetMode(display, ret, mode)) {
    // ERROR OUT
    return nullptr;
  }

  // TODO: Validate name is unique
  // TODO: Other validation ?
  // TODO: Register in the object manager
  return ret;
}
