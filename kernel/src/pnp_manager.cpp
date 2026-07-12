#include "pnp_manager.hpp"
#include "object_manager.hpp"

#include "stdkrn.hpp"
#include <Kernel.hpp>
#include <kernel/resource.hpp>
#include <new.hpp>

static kstd::array<kernel::KObjectPtr<kernel::KDriverObject>, 32> s_registeredDrivers;
static size_t s_registeredDriverCount = 0;
static kernel::KDevicePtr<kernel::KDeviceBus> s_rootDevice;

// Device-interest (class-trigger) registry: drivers that want to be attached as
// an upper filter whenever a device of a given type is started.
struct DeviceInterest {
  kernel::DeviceType type{kernel::device_type::Unknown};
  kernel::KObjectPtr<kernel::KDriverObject> driver;
};
static kstd::array<DeviceInterest, 32> s_deviceInterests;
static size_t s_deviceInterestCount = 0;

static kernel::KObjectPtr<kernel::KDriverObject> FindDriverForHardwareId(const kstd::string_view &hwid) {
  for (size_t i = 0; i < s_registeredDriverCount; i++) {
    if (s_registeredDrivers[i]->name == hwid) {
      return s_registeredDrivers[i];
    }
  }

  return nullptr;
}

namespace kernel::pnp {
void init() {
  // Create the kernel-owned root enumerator (\Device\Root). It has no driver
  // object; its child PDOs are tracked in the PnpRootContext extension.
  s_rootDevice = KE_IO_CreateBus(nullptr, 0, "System");
  early_print_fmt("PNP Manager initialized as {}.\r\n", s_rootDevice->Name());
}
}// namespace kernel::pnp

UNDOS_KERNEL_API_DEF kernel::KDevicePtr<kernel::KDeviceBus> KE_PNP_GetRootDevice() noexcept {
  return s_rootDevice;
}

UNDOS_KERNEL_API_DEF kernel::KDevicePtr<kernel::KDevice> KE_PNP_ReportRootDevice(const kstd::string_view &hardwareId) noexcept {
  // Create an unnamed PDO advertising the given hardware id.
  auto pdo = KE_IO_CreateDevice(
      nullptr,
      0,
      {},
      kernel::device_type::Unknown);
  if (!pdo) return nullptr;
  pdo->hardwareId = hardwareId;

  KE_PNP_ReportNewDevice(s_rootDevice, pdo);

  return pdo;
}

void KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept {
  if (s_registeredDriverCount < s_registeredDrivers.size()) {
    s_registeredDrivers[s_registeredDriverCount++] = driver;
  }
}

UNDOS_KERNEL_API_DEF void KE_PNP_RegisterDeviceInterest(
    kernel::DeviceType type,
    const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept {
  if (!driver || type == kernel::device_type::Unknown) return;
  if (s_deviceInterestCount < s_deviceInterests.size()) {
    s_deviceInterests[s_deviceInterestCount].type = type;
    s_deviceInterests[s_deviceInterestCount].driver = driver;
    s_deviceInterestCount++;
  }
}

// Attaches every driver that registered interest in the given FDO's device type
// as an upper filter (AddDevice) and starts it. This is the class-trigger that
// loads a secondary driver (e.g. volmgr) when a device type appears.
static void TriggerDeviceInterest(const kernel::KDevicePtr<kernel::KDevice> &fdo) {
  if (!fdo) return;

  for (size_t i = 0; i < s_deviceInterestCount; i++) {
    if (s_deviceInterests[i].type != fdo->deviceType) continue;
    const auto &driver = s_deviceInterests[i].driver;
    if (!driver || !driver->AddDevice) continue;
    if (driver->AddDevice(driver, fdo)) {
      early_print_fmt("PnP: Attached interest driver {} to device (type hash 0x{x})\n\r", driver->name, fdo->deviceType.value);
      if (driver->StartDevice) {
        driver->StartDevice(fdo);
      }
    }
  }
}

void KE_PNP_ReportNewDevice(
    const kernel::KDevicePtr<kernel::KDeviceBus> &parent,
    const kernel::KDevicePtr<kernel::KDevice> &pdo) noexcept {
  if (!pdo) [[unlikely]] {
    early_print_fmt("KE_PNP_ReportNewDevice: PDO is null.\n\r");
    return;
  }

  early_print_fmt("KE_PNP_ReportNewDevice: PDO {} (type hash 0x{x})\n\r", pdo->Name(), pdo->deviceType.value);

  // Parent tracking
  if (parent) {
    KE_IO_AttachChildDevice(parent, pdo);
  }

  // Identification: prefer the explicit hardwareId field (set on root/legacy
  // PDOs that have no driver), otherwise fall back to the enumerating driver's
  // GetHardwareId callback.
  kstd::string_view hwid;
  if (!pdo->hardwareId.empty()) {
    hwid = pdo->hardwareId;
  } else if (pdo->driverObject && pdo->driverObject->GetHardwareId) {
    hwid = pdo->driverObject->GetHardwareId(pdo);
  } else {
    early_print_fmt("KE_PNP_ReportNewDevice: PDO {} has no hardware ID.\r\n", pdo->Name());
    return;
  }

  // Matching
  if (const auto driver = FindDriverForHardwareId(hwid)) {
    // Capture the child's resource requirements from its *enumerating* driver
    // BEFORE AddDevice rebinds pdo->driverObject to the function driver. The
    // enumerator (e.g. the ISA bus) is the one that knows the controller's
    // fixed ports/IRQ.
    kernel::IoResourceRequirementsList *req = nullptr;
    if (pdo->driverObject && pdo->driverObject->QueryResourceRequirements) {
      req = pdo->driverObject->QueryResourceRequirements(pdo);
    }

    // Binding
    if (driver->AddDevice) {
      if (driver->AddDevice(driver, pdo)) {
        pdo->driverObject = driver;
        early_print_fmt("PnP: Bound driver {} to device {}\n\r", driver->name, hwid);

        // Reserve the device's fixed resources before starting it. A conflict
        // (an overlapping range already held by another device) leaves the
        // device un-started.
        if (!KE_RES_AssignResources(pdo, req)) {
          early_print_fmt("PnP: resource conflict for {}; not starting\n\r", hwid);
        } else {
          // Start Device. The event/dispatch layer was removed in favour of raw
          // method calls (see note in pnp.hpp); StartDevice is invoked directly.
          if (pdo->driverObject && pdo->driverObject->StartDevice) {
            pdo->driverObject->StartDevice(pdo);
          }

          // If the driver attached a Bus-type FDO, auto-enumerate its children.
          if (pdo->attachedDevice && pdo->attachedDevice->deviceType == kernel::device_type::Bus) {
            KE_PNP_EnumerateBus(pdo->attachedDevice.As<kernel::KDeviceBus>());
          }

          // Class-trigger: attach any driver that registered interest in the
          // (just-started) function device object's type as an upper filter.
          TriggerDeviceInterest(pdo->attachedDevice ? pdo->attachedDevice : pdo);
        }
      }
    }

    // The requirements list is only needed for the reservation above; free it
    // once assignment is done (mirrors the ScatterGatherList lifecycle).
    KE_RES_FreeRequirementsList(req);
  } else {
    early_print_fmt("PnP: No driver for {}; continuing with class triggers\n\r", hwid);
    TriggerDeviceInterest(pdo);
  }
}


void KE_PNP_EnumerateBus(const kernel::KDevicePtr<kernel::KDeviceBus> &busDevice) noexcept {
  if (!busDevice || !busDevice->driverObject) return;

  // 1. Trigger hardware enumeration
  if (busDevice->driverObject->EnumerateDevices) {
    busDevice->driverObject->EnumerateDevices(busDevice);
  }

  // 2. Query discovered children
  if (busDevice->driverObject->GetChildCount && busDevice->driverObject->GetChild) {
    const size_t num_children = busDevice->driverObject->GetChildCount(busDevice);
    for (size_t i = 0; i < num_children; i++) {
      if (const auto child = busDevice->driverObject->GetChild(busDevice, i)) {
        KE_PNP_ReportNewDevice(busDevice, child);
      }
    }
  }
}
