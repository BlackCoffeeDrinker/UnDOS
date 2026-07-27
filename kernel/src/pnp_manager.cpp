#include "pnp_manager.hpp"
#include "stdkrn.hpp"

#include <kernel/mutex.hpp>
#include <list.hpp>
#include <mutex.hpp>

namespace {
// Device-interest (class-trigger) registry: drivers that want to be attached as
// an upper filter whenever a device of a given type is started.
struct DeviceInterest {
  kernel::DeviceType type{kernel::device_type::Unknown};
  kernel::KObjectPtr<kernel::KDriverObject> driver;
};

// Guards s_deviceInterests/s_registeredDrivers, which are now read from the
// PnP worker thread while potentially still mutated from the boot thread as
// drivers load.
kernel::Mutex s_registryLock;
kstd::list<DeviceInterest> s_deviceInterests;
kstd::list<kernel::KObjectPtr<kernel::KDriverObject>> s_registeredDrivers;
kernel::KDevicePtr<kernel::KDeviceBus> s_rootDevice;

// Background work queue: KE_PNP_ReportNewDevice only attaches the PDO to the
// tree and pushes a work item here; PnPThread does the actual bind/start/
// enumerate/trigger work so the reporting driver returns immediately.
struct PnpWorkItem {
  kernel::KDevicePtr<kernel::KDeviceBus> parent;
  kernel::KDevicePtr<kernel::KDevice> pdo;
};

kernel::Mutex s_queueLock;
kstd::list<PnpWorkItem> s_workQueue;
bool s_workerBusy = false;
kernel::KObjectPtr<kernel::KThreadObject> s_pnpThread;
}// namespace

static kernel::KObjectPtr<kernel::KDriverObject> FindDriverForHardwareId(const kstd::string_view &hwid) {
  kstd::unique_lock guard(s_registryLock);
  for (const auto &driver: s_registeredDrivers) {
    if (driver->name == hwid) {
      return driver;
    }
  }

  return nullptr;
}

static void ProcessDevice(
    const kernel::KDevicePtr<kernel::KDeviceBus> &parent,
    const kernel::KDevicePtr<kernel::KDevice> &pdo);
static void TriggerDeviceInterest(const kernel::KDevicePtr<kernel::KDevice> &fdo);

namespace kernel::pnp {

void PnPThread(void *) {
  while (true) {
    PnpWorkItem item;
    bool hasItem = false;

    {
      kstd::unique_lock guard(s_queueLock);
      if (s_workQueue.empty()) {
        s_workerBusy = false;
      } else {
        item = s_workQueue.front();
        s_workQueue.pop_front();
        s_workerBusy = true;
        hasItem = true;
      }
    }

    if (hasItem) {
      ProcessDevice(item.parent, item.pdo);

      kstd::unique_lock guard(s_queueLock);
      if (s_workQueue.empty()) {
        s_workerBusy = false;
      }
    } else {
      KE_SCHED_Block();
    }
  }
}

void init() {
  // Create the kernel-owned root enumerator (\Device\Root). It has no driver
  // object; its child PDOs are tracked in the PnpRootContext extension.
  s_rootDevice = KE_IO_CreateBus(nullptr, 0, "System");
  early_print_fmt("PNP Manager initialized as {}.\r\n", s_rootDevice->Name());

  s_pnpThread = KE_SCHED_CreateThread(&PnPThread, nullptr, 0);
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
  kstd::unique_lock guard(s_registryLock);
  s_registeredDrivers.push_back(driver);
}

UNDOS_KERNEL_API_DEF void KE_PNP_RegisterDeviceInterest(
    kernel::DeviceType type,
    const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept {
  if (!driver || type == kernel::device_type::Unknown) return;
  if (!driver->AddDevice) {
    early_print_fmt("KE_PNP_RegisterDeviceInterest: ERROR Driver {} does not implement AddDevice, cannot register interest\n\r", driver->name);
    return;
  }

  kstd::unique_lock guard(s_registryLock);
  s_deviceInterests.emplace_back(type, driver);
}

// Attaches every driver that registered interest in the given FDO's device type
// as an upper filter (AddDevice) and starts it. This is the class-trigger that
// loads a secondary driver (e.g. volmgr) when a device type appears.
static void TriggerDeviceInterest(const kernel::KDevicePtr<kernel::KDevice> &fdo) {
  if (!fdo) return;

  if (fdo->pnpState != kernel::KPnpState::Started) {
    early_print_fmt("WARN: Device {} is not started: future versions will discard trigger\n\r", fdo->Name());
  }

  kstd::unique_lock guard(s_registryLock);
  for (const auto &interest: s_deviceInterests) {
    if (interest.type != fdo->deviceType) {
      continue;
    }
    const auto &driver = interest.driver;
    if (!driver || !driver->AddDevice) [[unlikely]] {
      // This shouldn't happen as checks in KE_PNP_RegisterDeviceInterest prevent this
      continue;
    }

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

  // Parent tracking: the tree shape must be visible immediately, so this part
  // stays synchronous. Everything else (identify, bind, resources, start,
  // recursive enumerate, class-trigger) is deferred to the PnP worker thread.
  if (parent) {
    KE_IO_AttachChildDevice(parent, pdo);
  }

  {
    kstd::unique_lock guard(s_queueLock);
    s_workQueue.emplace_back(PnpWorkItem{parent, pdo});
  }

  if (s_pnpThread) {
    KE_SCHED_Unblock(s_pnpThread);
  }
}

UNDOS_KERNEL_API_DEF void KE_PNP_WaitForQuiescence() noexcept {
  while (true) {
    {
      kstd::unique_lock guard(s_queueLock);
      if (s_workQueue.empty() && !s_workerBusy) {
        return;
      }
    }
    KE_SCHED_Yield();
  }
}

static void ProcessDevice(
    const kernel::KDevicePtr<kernel::KDeviceBus> &parent,
    const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  (void) parent;

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
          pdo->pnpState = kernel::KPnpState::Stopped;
        } else {
          // Start Device. The event/dispatch layer was removed in favour of raw
          // method calls (see note in pnp.hpp); StartDevice is invoked directly.
          if (pdo->driverObject && pdo->driverObject->StartDevice) {
            pdo->driverObject->StartDevice(pdo);
          }

          pdo->pnpState = kernel::KPnpState::Started;

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
    pdo->pnpState = kernel::KPnpState::Started;
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
