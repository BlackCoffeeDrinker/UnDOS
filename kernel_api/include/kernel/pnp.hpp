#pragma once

#include <kernel/io.hpp>
#include <kernel/event.hpp>

UNDOS_KERNEL_API void Ke_PNP_RegisterDriver(kernel::KObjectPtr<kernel::KDriverObject> driver);
UNDOS_KERNEL_API void Ke_PNP_ReportNewDevice(kernel::KObjectPtr<kernel::KDeviceObject> parent, kernel::KObjectPtr<kernel::KDeviceObject> pdo);
UNDOS_KERNEL_API void Ke_PNP_DispatchEvent(kernel::KObjectPtr<kernel::KDeviceObject> device, const kernel::KEvent &event);
UNDOS_KERNEL_API void Ke_PNP_EnumerateBus(kernel::KObjectPtr<kernel::KDeviceObject> busDevice);
