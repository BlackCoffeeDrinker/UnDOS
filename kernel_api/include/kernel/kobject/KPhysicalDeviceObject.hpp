#pragma once

#include <kernel/kobject/DeviceType.hpp>
#include <kernel/kobject/KDriverObject.hpp>
#include <kernel/kobject/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <static_string.hpp>
#include <utility.hpp>

namespace kernel {

/**
 * @brief Represents a Physical Device Object (PDO) in the device tree.
 * 
 * A KPhysicalDeviceObject is typically created by a bus driver for each child device
 * it discovers on its bus.
 */
struct KPhysicalDeviceObject : KObjectT<KPhysicalDeviceObject, 1, TYPE_BUS_DEVICE> {
  KObjectPtr<KDriverObject> driverObject;
  KObjectPtr<KPhysicalDeviceObject> attachedDevice;
  KObjectPtr<KPhysicalDeviceObject> lowerDevice;
  data_buffer deviceExtension;
  DeviceType deviceType{device_type::Unknown};
  uint32_t flags{0};

  KPhysicalDeviceObject(KObjectPtr<KDriverObject> driver) : driverObject(kstd::move(driver)) {}
};

}// namespace kernel
