#include "event_dispatcher.hpp"

namespace kernel {

void Ke_Event_DispatchToDevice(kernel::KObjectPtr<kernel::KDeviceObject> device, const kernel::KEvent &event) {
  if (device && device->driverObject) {
    Ke_Event_DispatchToDriver(device->driverObject, event);
  }
}

void Ke_Event_DispatchToDriver(kernel::KObjectPtr<kernel::KDriverObject> driver, const kernel::KEvent &event) {
  if (driver && driver->eventHandler) {
    driver->eventHandler(driver, event);
  }
}

}// namespace kernel
