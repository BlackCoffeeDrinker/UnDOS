#include "stdkrn.hpp"
#include "event_broadcaster.hpp"
#include "gfx/gfx.hpp"

UNDOS_KERNEL_API_DEF kstd::error_code KE_PNP_ReportRemovedDevice(
    const kernel::KDevicePtr<kernel::KDevice> &device) noexcept {
  if (!device) return kernel::KernelError::InvalidArgument;
  auto base = device;
  while (base->lowerDevice) base = base->lowerDevice;
  kernel::gfx::remove_device(base);
  kstd::error_code result{};
  for (auto current = base; current; current = current->attachedDevice) {
    const auto error = kernel::event::broadcast(
        current, kernel::event::make_event_code(
            KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Stopped));
    if (error) result = error;
  }
  return result;
}
