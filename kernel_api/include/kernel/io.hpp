#pragma once

#include <kernel/kobject/KPhysicalDeviceObject.hpp>
#include <utility.hpp>

UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KPhysicalDeviceObject> KE_IO_CreateDevice(
  kernel::KObjectPtr<kernel::KDriverObject> driver,
  size_t deviceExtensionSize,
  const kstd::string_view& deviceName,
  kernel::DeviceType deviceType);
