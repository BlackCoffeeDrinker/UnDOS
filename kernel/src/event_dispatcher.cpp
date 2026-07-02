#include "event_dispatcher.hpp"

#include <Kernel.hpp>

void KE_EVENT_DispatchToDevice(const kernel::KObjectPtr<kernel::KDeviceObject>& device, const kernel::KEvent &event) {
  if (device && device->driverObject) {
    KE_EVENT_DispatchToDriver(device->driverObject, event);
  }
}

void KE_EVENT_DispatchToDriver(const kernel::KObjectPtr<kernel::KDriverObject>& driver, const kernel::KEvent &event) {
  if (driver && driver->eventHandler) {
    driver->eventHandler(driver, event);
  }
}
