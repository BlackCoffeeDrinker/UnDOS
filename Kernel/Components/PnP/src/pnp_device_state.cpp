#include "stdkrn.hpp"
#include <kernel/ob/event_broadcaster.hpp>

UNDOS_KERNEL_API_DEF kstd::error_code KE_PNP_ReportRemovedDevice(
    const kernel::KDevicePtr<kernel::KDevice> &device) noexcept {
  if (!device) return kernel::KernelError::InvalidArgument;
  auto base = device;
  while (base->lowerDevice) base = base->lowerDevice;
  // Subscribers (e.g. GFX) let go of the stack before it is marked removed
  // and before the caller tears the hardware down.
  for (auto current = base; current; current = current->attachedDevice)
    kernel::event::broadcast_sync(
        current, kernel::event::make_event_code(
            KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Removing));
  for (auto current = base; current; current = current->attachedDevice)
    current->pnpState = kernel::KPnpState::Removed;
  kstd::error_code result{};
  for (auto current = base; current; current = current->attachedDevice) {
    const auto error = kernel::event::broadcast(
        current, kernel::event::make_event_code(
            KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Stopped));
    if (error) result = error;
  }
  return result;
}
