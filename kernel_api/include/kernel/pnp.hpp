#pragma once

#include <kernel/io.hpp>

// NOTE: The KEvent/KPnpEvent dispatch layer was removed for now in favour of
// raw method calls (e.g. the PnP manager calls driver->StartDevice directly).
// If a richer event/IRP-style model is needed later, events can be reintroduced.

/**
 * @ingroup PNP
 * @brief Method KE_PNP_ReportNewDevice
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_PNP_ReportNewDevice,
    const kernel::KDevicePtr<kernel::KDeviceBus> &parent, const kernel::KDevicePtr<kernel::KDevice> &pdo);

/**
 * @ingroup PNP
 * @brief Method KE_PNP_EnumerateBus
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_PNP_EnumerateBus,
    const kernel::KDevicePtr<kernel::KDeviceBus> &busDevice);

/**
 * @ingroup PNP
 * @brief Method KE_PNP_RegisterDeviceInterest
 *
 * Registers a driver's interest in a device type (class). When a function
 * driver later starts a device whose FDO has the given deviceType, the PnP
 * manager attaches the interested driver as an upper filter (AddDevice) and
 * starts it. This is the "detecting a device type triggers another driver"
 * (Windows-style filter loading) mechanism.
 * 
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_PNP_RegisterDeviceInterest,
    kernel::DeviceType type, const kernel::KObjectPtr<kernel::KDriverObject> &driver);


/**
 * @ingroup PNP
 * @brief Method KE_PNP_ReportRootDevice
 * 
 * Creates an unnamed root PDO carrying the given hardware id, records it as a
 * child of the kernel-owned \Device\Root enumerator, and reports it so a
 * matching driver can bind (used by the HAL to spawn legacy bus PDOs).
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KDevicePtr<kernel::KDevice>,
    KE_PNP_ReportRootDevice,
    const kstd::string_view &hardwareId);

/**
 * @ingroup PNP
 * @brief Method KE_PNP_GetRootDevice
 *
 * Returns the kernel-owned root enumerator device (\Device\Root).
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KDevicePtr<kernel::KDeviceBus>,
    KE_PNP_GetRootDevice);
