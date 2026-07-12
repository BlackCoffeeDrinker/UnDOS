#pragma once

#include <kernel/device.hpp>
#include <kernel/kobject/KDeviceObject.hpp>
#include <kernel/kobject/KDriverObject.hpp>

#include <new.hpp>
#include <span.hpp>
#include <utility.hpp>

/**
 * @ingroup IO
 * @brief Method KE_IO_CreateDevice
 * 
 * Creates a device object. The device name is OPTIONAL (Windows-style): a
 * non-empty name inserts the device into the \Device object namespace (used
 * only for stable singletons), while an empty name yields an unnamed device
 * that never enters the namespace and therefore cannot collide with siblings.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KDevicePtr<kernel::KDevice>,
    KE_IO_CreateDevice,
    kernel::KObjectPtr<kernel::KDriverObject> driver, size_t deviceExtensionSize, const kstd::string_view &deviceName, kernel::DeviceType deviceType);

/**
 * @ingroup IO
 * @brief Method KE_IO_CreateBus
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KDevicePtr<kernel::KDeviceBus>,
    KE_IO_CreateBus,
    kernel::KObjectPtr<kernel::KDriverObject> driver, size_t deviceExtensionSize, const kstd::string_view &busName);

/**
 * @ingroup IO
 * @brief Method KE_IO_AttachFilterDevice
 *
 * Attaches sourceDevice on top of targetDevice's device stack, setting
 * targetDevice->attachedDevice = sourceDevice, and returns the device that
 * sourceDevice now sits on top of (assign it to sourceDevice->lowerDevice).
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KDevicePtr<kernel::KDevice>,
    KE_IO_AttachFilterDevice,
    const kernel::KDevicePtr<kernel::KDevice> &sourceDevice, const kernel::KDevicePtr<kernel::KDevice> &targetDevice);

/**
 * @ingroup IO
 * @brief Method KE_IO_AttachChildDevice
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KDevicePtr<kernel::KDevice>,
    KE_IO_AttachChildDevice,
    const kernel::KDevicePtr<kernel::KDeviceBus> &bus, kernel::KDevicePtr<kernel::KDevice> child);


/**
 * @ingroup IO
 * @brief Method KE_IO_ReadDevice
 * 
 * Synchronous byte-offset block read/write dispatch. Validates arguments and
 * delegates to the device's driver Read/Write handler. Returns
 * IoStatus::Unsupported when the device's driver has no handler.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::IoStatus,
    KE_IO_ReadDevice,
    const kernel::KDevicePtr<kernel::KDevice> &device, uint64_t offset, kstd::span<uint8_t> buffer, size_t &transferred);

/**
 * @ingroup IO
 * @brief Method KE_IO_WriteDevice
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::IoStatus,
    KE_IO_WriteDevice,
    const kernel::KDevicePtr<kernel::KDevice> &device, uint64_t offset, kstd::span<const uint8_t> buffer, size_t &transferred);


template<typename T>
kernel::KDevicePtr<kernel::KDevice> KE_IO_CreateDeviceWithContext(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    const kstd::string_view &deviceName,
    kernel::DeviceType deviceType) {
  if (const auto dev = KE_IO_CreateDevice(kstd::move(driver), sizeof(T), deviceName, deviceType)) {
    new (dev->deviceExtension.as<T *>()) T();
    return dev;
  }
  return nullptr;
}

template<typename T>
kernel::KDevicePtr<kernel::KDeviceBus> KE_IO_CreateBusWithContext(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    const kstd::string_view &busName = {}) {
  if (auto dev = KE_IO_CreateBus(kstd::move(driver), sizeof(T), busName)) {
    new (dev->deviceExtension.as<T *>()) T();
    return dev;
  }
  return nullptr;
}
