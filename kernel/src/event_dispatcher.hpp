#pragma once

#include <kernel/io.hpp>
#include <kernel/event.hpp>

namespace kernel {

class EventDispatcher {
public:
  static void DispatchToDevice(KObjectPtr<KDeviceObject> device, const KEvent &event);
  static void DispatchToDriver(KObjectPtr<KDriverObject> driver, const KEvent &event);
};

} // namespace kernel
