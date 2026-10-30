#pragma once

#include <kernel/event_broadcast.hpp>
#include <kernel/io.hpp>
#include <kernel/kerror.hpp>
#include <expected.hpp>

namespace kernel {
using ReportRootDeviceResult = kstd::expected<KDevicePtr<KDevice>, kstd::error_code>;
}

// NOTE: The KEvent/KPnpEvent dispatch layer was removed for now in favour of
// raw method calls (e.g. the PnP manager calls driver->StartDevice directly).
// If a richer event/IRP-style model is needed later, events can be reintroduced.

namespace kernel::pnp {
// Broadcast on the started function device after its PnP state is updated.
// Subscribers register interest in event::Source(fdo).
enum class DeviceEvent : uint32_t {
  Started = 1,
  Stopped = 2,
  Changed = 3,
};
}// namespace kernel::pnp
namespace kernel::event {
template<>
struct is_code_enum<pnp::DeviceEvent> : kstd::true_type {};
}

// Report before hardware/resource teardown, in preemptible context outside
// USER/topology locks. Drains USER drawing and retires graphics targets before
// marking the retained stack removed, then queues notifications.
// Pinned device extensions remain alive for cleanup.
// This does not free device objects or reclaim driver resources.
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_PNP_ReportRemovedDevice,
    const kernel::KDevicePtr<kernel::KDevice> &device);

UNDOS_KERNEL_PUBLIC_V1API(
    const kernel::event::Category &,
    KE_PNP_EventCategory);

/**
 * @ingroup PNP
 * @brief Method KE_PNP_ReportNewDevice
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_PNP_ReportNewDevice,
    const kernel::KDevicePtr<kernel::KDeviceBus> &parent, const kernel::KDevicePtr<kernel::KDevice> &pdo);

/**
 * @ingroup PNP
 * @brief Method KE_PNP_EnumerateBus
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
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
    kstd::error_code,
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
    kernel::ReportRootDeviceResult,
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

/**
 * @ingroup PNP
 * @brief Method KE_PNP_WaitForQuiescence
 *
 * Blocks the calling thread until the PnP work queue is empty and the PnP
 * worker thread is not mid-processing an item. Used at boot, right after the
 * device tree enumeration is kicked off, to preserve the guarantee that the
 * root device tree is fully started before the root filesystem is looked up.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_PNP_WaitForQuiescence);
