#pragma once

#include <kernel/event.hpp>
#include <kernel/io.hpp>


void KE_EVENT_DispatchToDevice(const kernel::KObjectPtr<kernel::KDeviceObject>& device, const kernel::KEvent &event);
void KE_EVENT_DispatchToDriver(const kernel::KObjectPtr<kernel::KDriverObject>& driver, const kernel::KEvent &event);
