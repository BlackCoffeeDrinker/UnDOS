#include "event_dispatcher.hpp"

namespace kernel {

void EventDispatcher::DispatchToDevice(KObjectPtr<KDeviceObject> device, const KEvent &event) {
  if (device && device->driverObject) {
    DispatchToDriver(device->driverObject, event);
  }
}

void EventDispatcher::DispatchToDriver(KObjectPtr<KDriverObject> driver, const KEvent &event) {
  if (driver && driver->eventHandler) {
    driver->eventHandler(driver, event);
  }
}

} // namespace kernel
