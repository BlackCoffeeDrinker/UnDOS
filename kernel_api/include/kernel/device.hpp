
#pragma once

#include <kernel/__core.hpp>
#include <kernel/StaticIdentifier.hpp>
#include <kernel/data_buffer.hpp>
#include <kernel/kobject/KDeviceObject.hpp>
#include <kernel/kobject/KDriverObject.hpp>

namespace kernel {
struct DeviceTypeTag {
  // Unique seed for DeviceType to minimize collisions
  static constexpr uint64_t seed = 0x5a1e7b1d9c2f4a3bull;
};

using DeviceType = StaticIdentifier<DeviceTypeTag>;

consteval DeviceType operator""_dev(const char *s, size_t n) {
  return DeviceType::from_string(kstd::string_view{s, n});
}

namespace device_type {
constexpr auto Unknown = DeviceType{0};
constexpr auto Bus = "bus"_dev;
constexpr auto DiskController = "disk_controller"_dev;
constexpr auto Disk = "disk"_dev;
constexpr auto Keyboard = "keyboard"_dev;
constexpr auto Mouse = "mouse"_dev;
constexpr auto Video = "video"_dev;
constexpr auto Serial = "serial"_dev;
constexpr auto Parallel = "parallel"_dev;
constexpr auto Volume = "volume"_dev;
}// namespace device_type


enum class KPnpState : uint8_t {
  NotStarted,
  Started,
  Stopped,
  Removed,
};

struct KDeviceBus;

struct KDevice : Versioned<KDevice, 1> {
  KObjectPtr<KDriverObject> driverObject{nullptr};//< Driver that controls this device
  KObjectPtr<KDeviceObject> deviceObject{nullptr};//< If this device is registered in the object manager, a pointer to it
  KPnpState pnpState{KPnpState::NotStarted};      //< Current PnP state of the device
  kstd::static_string<32> hardwareId{};           //< Hardware Identifier for lazy lookup (Devices created by the HAL)
  DeviceType deviceType = device_type::Unknown;   //< RTTIless type id
  DataBuffer deviceExtension{};                   //< Driver owned data
  KDevicePtr<KDeviceBus> parentBus{nullptr};      //< Near parent bus (optional)

  // region Vertical Stack (Filters)
  KDevicePtr<KDevice> lowerDevice{nullptr};   //< Device below this one (optional)
  KDevicePtr<KDevice> attachedDevice{nullptr};//< device above this one
  // endregion

  KDevice() = default;
  KDevice(const KObjectPtr<KDriverObject> &driver) : driverObject(driver) {}

  [[nodiscard]] kstd::string_view Name() const noexcept {
    return deviceObject
               ? kstd::string_view{deviceObject->name}
               : kstd::string_view{};
  }
};

struct KDeviceBus : KDevice {
  kstd::array<KDevicePtr<KDevice>, 64> children{};
  uint8_t childCount{0};
};

}// namespace kernel
