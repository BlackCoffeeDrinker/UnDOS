#pragma once

#include <kernel/device.hpp>
#include <kernel/kobject/KDeviceObject.hpp>
#include <kernel/kobject/KDriverObject.hpp>
#include <kernel/kobject/KInterruptServiceRoutineObject.hpp>

#include <new.hpp>
#include <span.hpp>
#include <utility.hpp>

enum class CreateDeviceFlags : uint16_t {
  None = 0,
  MultipleAllowed = 1 << 1,//< Treats name as a base name and appends a number to it to make it unique
};

inline CreateDeviceFlags operator|(CreateDeviceFlags lhs, CreateDeviceFlags rhs) { return static_cast<CreateDeviceFlags>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs)); }
inline bool operator&(CreateDeviceFlags a, CreateDeviceFlags b) { return (static_cast<uint16_t>(a) & static_cast<uint16_t>(b)) != 0; }

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
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    size_t deviceExtensionSize, const kstd::string_view &baseDeviceName,
    kernel::DeviceType deviceType, CreateDeviceFlags flags = CreateDeviceFlags::None);

/**
 * @ingroup IO
 * @brief Method KE_IO_CreateBus
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KDevicePtr<kernel::KDeviceBus>,
    KE_IO_CreateBus,
    kernel::KObjectPtr<kernel::KDriverObject> driver, size_t deviceExtensionSize,
    const kstd::string_view &busName, CreateDeviceFlags flags = CreateDeviceFlags::None);

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

/**
 * @ingroup IO
 * @brief Method KE_IO_ControlDevice
 *
 * Synchronous control dispatch to the device's driver DeviceControl handler.
 * Returns IoStatus::Unsupported when the driver has no handler.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::IoStatus,
    KE_IO_ControlDevice,
    const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t code,
    kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred);

/**
 * @ingroup IO
 * @brief Method KE_IO_ConnectInterrupt
 *
 * Builds a KInterruptServiceRoutineObject wrapping `routine`/`context`,
 * connects it to `irq` via HAL_INTR_ConnectInterrupt (which also unmasks
 * the line), and returns it. The caller (typically the driver's AddDevice
 * or StartDevice) MUST keep the returned KObjectPtr alive for as long as
 * the interrupt should stay connected -- e.g. by storing it in the
 * driver's device extension -- since dropping it frees a live ISR object.
 * Returns nullptr on allocation failure or if `irq` is already connected.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject>,
    KE_IO_ConnectInterrupt,
    const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t irq,
    kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)> routine,
    void *context);

/**
 * @ingroup IO
 * @brief Method KE_IO_DisconnectInterrupt
 *
 * Disconnects `irq` (re-masking the line via HAL_INTR_DisconnectInterrupt).
 * Does not free the KObjectPtr itself -- that happens when the caller's
 * last reference to it is dropped.
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_IO_DisconnectInterrupt, uint32_t irq);


template<typename T>
kernel::KDevicePtr<kernel::KDevice> KE_IO_CreateDeviceWithContext(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    const kstd::string_view &deviceName,
    kernel::DeviceType deviceType, CreateDeviceFlags flags = CreateDeviceFlags::None) {
  if (const auto dev = KE_IO_CreateDevice(kstd::move(driver), sizeof(T), deviceName, deviceType, flags)) {
    new (dev->deviceExtension.as<T *>()) T();
    return dev;
  }
  return nullptr;
}

template<typename T>
kernel::KDevicePtr<kernel::KDeviceBus> KE_IO_CreateBusWithContext(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    const kstd::string_view &busName = {}, CreateDeviceFlags flags = CreateDeviceFlags::None) {
  if (auto dev = KE_IO_CreateBus(kstd::move(driver), sizeof(T), busName, flags)) {
    new (dev->deviceExtension.as<T *>()) T();
    return dev;
  }
  return nullptr;
}
