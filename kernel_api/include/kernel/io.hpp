#pragma once

#include <kernel/KObject.hpp>
#include <utility.hpp>

namespace kernel {

struct KDriverObject;

enum class DeviceType : uint32_t {
  Unknown = 0,
  Bus = 1,
  Disk = 2,
  Keyboard = 3,
  Mouse = 4,
  Video = 5,
  Serial = 6,
  Parallel = 7,
};

struct KDeviceObject : KObjectT<KDeviceObject, 1, TYPE_DEVICE> {
  KObjectPtr<KDriverObject> driverObject;
  KObjectPtr<KDeviceObject> attachedDevice;
  KObjectPtr<KDeviceObject> lowerDevice;
  void *deviceExtension{nullptr};
  DeviceType deviceType{DeviceType::Unknown};
  uint32_t flags{0};

  kstd::static_string<128> hardwareId;

  KDeviceObject(KObjectPtr<KDriverObject> driver) : driverObject(kstd::move(driver)) {}
};

} // namespace kernel
