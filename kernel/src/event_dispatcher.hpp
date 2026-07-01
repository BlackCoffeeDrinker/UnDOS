#pragma once

#include <kernel/event.hpp>
#include <kernel/io.hpp>


namespace kernel {
void Ke_Event_DispatchToDevice(kernel::KObjectPtr<kernel::KDeviceObject> device, const kernel::KEvent &event);
void Ke_Event_DispatchToDriver(kernel::KObjectPtr<kernel::KDriverObject> driver, const kernel::KEvent &event);
}// namespace kernel
