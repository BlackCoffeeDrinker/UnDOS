#pragma once

#include <kernel/kobject/KDriverObject.hpp>
#include <kernel/kobject/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <utility.hpp>

namespace kernel {

struct KPhysicalDeviceObject;

/**
 * @brief Represents a Functional Device Object (FDO) in the device tree.
 * 
 * A KFunctionalDeviceObject is created by a function driver to manage the
 * functionality of a device. It is typically attached to a PDO (KPhysicalDeviceObject) 
 * or another device object in the stack.
 */
struct KFunctionalDeviceObject : KObjectT<KFunctionalDeviceObject, 1, TYPE_FUNCTIONAL_DEVICE> {
  KObjectPtr<KDriverObject> driverObject;
  KObjectPtr<KPhysicalDeviceObject> pdo;
  
  // Note: These might need to be more generic if we support deep stacks
  KObjectPtr<KPhysicalDeviceObject> attachedDevice;
  KObjectPtr<KPhysicalDeviceObject> lowerDevice;
  
  void *deviceExtension{nullptr};
  uint32_t flags{0};

  KFunctionalDeviceObject(KObjectPtr<KDriverObject> driver, KObjectPtr<KPhysicalDeviceObject> physicalDevice) 
    : driverObject(kstd::move(driver)), pdo(kstd::move(physicalDevice)) {}
};

} // namespace kernel
