#pragma once

#include <kernel/io.hpp>
#include <kernel/event.hpp>

UNDOS_KERNEL_API void KE_PNP_ReportNewDevice(const kernel::KObjectPtr<kernel::KDeviceObject>& parent, const kernel::KObjectPtr<kernel::KDeviceObject>& pdo);
UNDOS_KERNEL_API void KE_PNP_DispatchEvent(const kernel::KObjectPtr<kernel::KDeviceObject>& device, const kernel::KEvent &event);
UNDOS_KERNEL_API void KE_PNP_EnumerateBus(const kernel::KObjectPtr<kernel::KDeviceObject>& busDevice);
