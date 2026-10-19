#include "pnp_manager.hpp"
#include "event_broadcaster.hpp"
#include "stdkrn.hpp"

#include <kernel/kobject/KDispatcherHeader.hpp>
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

// Auto-reset dispatcher header used to park the worker. The signal is sticky
// (KDispatcherHeader::TryAcquire consumes it), so a KE_PNP_ReportNewDevice
// that lands between the worker observing an empty queue and actually going to
// sleep cannot be lost - which the old KE_SCHED_Block/Unblock pair could not
// guarantee.
kernel::KDispatcherHeader s_workAvailable{};
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
static void TriggerDeviceStateChanged(const kernel::KDevicePtr<kernel::KDevice> &fdo);
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
      const auto waitStatus = KE_SCHED_WaitForHeader(s_workAvailable, kernel::WAIT_INFINITE);
      if (waitStatus != kernel::KernelError::Success) {
        KE_Print("PnP worker wait failed: {}", kernel::make_error_code(waitStatus).message());
        KE_SCHED_Yield();
      }
    }
  }
}

KernelError init() {
  // Create the kernel-owned root enumerator (\Device\Root). It has no driver
  // object; its child PDOs are tracked in the PnpRootContext extension.
  auto root = KE_IO_CreateBus(nullptr, 0, "System");
  if (!root) return root.error();
  s_rootDevice = *root;
  s_workAvailable.manualReset = false;
  s_workAvailable.signalled = false;
  auto worker = KE_SCHED_CreateThread(&PnPThread, nullptr, 0);
  if (!worker) return worker.error();
  s_pnpThread = *worker;
  return KernelError::Success;
}
}// namespace kernel::pnp

UNDOS_KERNEL_API_DEF kernel::KDevicePtr<kernel::KDeviceBus> KE_PNP_GetRootDevice() noexcept {
  return s_rootDevice;
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KDevicePtr<kernel::KDevice>, kernel::KernelError>
KE_PNP_ReportRootDevice(const kstd::string_view &hardwareId) noexcept {
  // Create an unnamed PDO advertising the given hardware id.
  auto pdo = KE_IO_CreateDevice(
      nullptr,
      0,
      {},
      kernel::device_type::Unknown);
  if (!pdo) return kstd::unexpected(pdo.error());
  pdo.value()->hardwareId = hardwareId;

  const auto status = KE_PNP_ReportNewDevice(s_rootDevice, *pdo);
  if (status != kernel::KernelError::Success) return kstd::unexpected(status);

  return *pdo;
}

kernel::KernelError KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept {
  if (!driver) return kernel::KernelError::InvalidArgument;
  kstd::unique_lock guard(s_registryLock);
  for (const auto &registered : s_registeredDrivers) {
    if (registered == driver) return kernel::KernelError::AlreadyExists;
  }
  s_registeredDrivers.push_back(driver);
  return kernel::KernelError::Success;
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_PNP_RegisterDeviceInterest(
    kernel::DeviceType type,
    const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept {
  if (!driver || type == kernel::device_type::Unknown) return kernel::KernelError::InvalidArgument;
  if (!driver->AddDevice) {
    KE_Print("KE_PNP_RegisterDeviceInterest: ERROR Driver {} does not implement AddDevice, cannot register interest", driver->name);
    return kernel::KernelError::Unsupported;
  }

  kstd::unique_lock guard(s_registryLock);
  s_deviceInterests.emplace_back(type, driver);
  return kernel::KernelError::Success;
}

static void TriggerDeviceStateChanged(const kernel::KDevicePtr<kernel::KDevice> &fdo) {
  if (fdo && fdo->pnpState == kernel::KPnpState::Started) {
    const auto status =
        kernel::event::broadcast(fdo, kernel::event::make_event_code(KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Started));
    if (status != kernel::KernelError::Success) {
      KE_Print("PnP started event broadcast failed: {}", kernel::make_error_code(status).message());
    }
  }
}

// Attaches every driver that registered interest in the given FDO's device type
// as an upper filter (AddDevice) and starts it. This is the class-trigger that
// loads a secondary driver (e.g. volmgr) when a device type appears.
static void TriggerDeviceInterest(const kernel::KDevicePtr<kernel::KDevice> &fdo) {
  if (!fdo) return;

  if (fdo->pnpState != kernel::KPnpState::Started) {
    KE_Print("WARN: Device {} is not started: future versions will discard trigger", fdo->Name());
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

    const auto addStatus = driver->AddDevice(driver, fdo);
    if (addStatus == kernel::KernelError::Success) {
      KE_Print("PnP: Attached interest driver {} to device (type hash 0x{x})", driver->name, fdo->deviceType.value);
      if (driver->StartDevice) {
        driver->StartDevice(fdo);
      }

      // AddDevice may have stacked its own filter FDO on top of fdo; that
      // top-of-stack device is the one actually started above, so mark it
      // (and fdo itself) as Started to keep pnpState accurate.
      fdo->pnpState = kernel::KPnpState::Started;
      if (fdo->attachedDevice) {
        fdo->attachedDevice->pnpState = kernel::KPnpState::Started;
      }

      // Broadcast that this driver/device has started (things like the GFX subsystem want to execute things after)
      TriggerDeviceStateChanged(fdo);
    } else {
      KE_Print("PnP: AddDevice failed for interest driver {}: {}", driver->name,
               kernel::make_error_code(addStatus).message());
    }
  }
}

UNDOS_KERNEL_API_DEF const kernel::event::Category &KE_PNP_EventCategory() noexcept {
  struct PnpEventCategory final : kernel::event::Category {
    [[nodiscard]] const char *name() const noexcept override { return "PnP"; }
    [[nodiscard]] kstd::string_view message(int) const noexcept override { return ""; }
    [[nodiscard]] bool equivalent(const kernel::event::Code &code) const noexcept override { return &code.category() == this; }
  };
  static PnpEventCategory category;

  return category;
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_PNP_ReportNewDevice(
    const kernel::KDevicePtr<kernel::KDeviceBus> &parent,
    const kernel::KDevicePtr<kernel::KDevice> &pdo) noexcept {
  if (!pdo) [[unlikely]] {
    KE_Print("KE_PNP_ReportNewDevice: PDO is null.");
    return kernel::KernelError::InvalidArgument;
  }

  KE_Print("KE_PNP_ReportNewDevice: PDO {} (type hash 0x{x})", pdo->Name(), pdo->deviceType.value);

  // Parent tracking: the tree shape must be visible immediately, so this part
  // stays synchronous. Everything else (identify, bind, resources, start,
  // recursive enumerate, class-trigger) is deferred to the PnP worker thread.
  if (parent) {
    const auto status = KE_IO_AttachChildDevice(parent, pdo);
    if (status != kernel::KernelError::Success) return status;
  }

  {
    kstd::unique_lock guard(s_queueLock);
    s_workQueue.emplace_back(PnpWorkItem{.parent = parent, .pdo = pdo});
  }

  // Sticky signal: safe even if the worker has not reached its wait yet, and
  // safe before init() created the thread at all.
  KE_EVENT_Signal(s_workAvailable, false);
  return kernel::KernelError::Success;
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
    KE_Print("KE_PNP_ReportNewDevice: PDO {} has no hardware ID.", pdo->Name());
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
      const auto requirements = pdo->driverObject->QueryResourceRequirements(pdo);
      if (!requirements) {
        KE_Print("PnP: failed to query resource requirements for {}: {}", hwid,
                 kernel::make_error_code(requirements.error()).message());
        return;
      }
      req = *requirements;
    }

    // Binding
    if (driver->AddDevice) {
      const auto addStatus = driver->AddDevice(driver, pdo);
      if (addStatus == kernel::KernelError::Success) {
        pdo->driverObject = driver;
        KE_Print("PnP: Bound driver {} to device {}", driver->name, hwid);

        // Reserve the device's fixed resources before starting it. A conflict
        // (an overlapping range already held by another device) leaves the
        // device un-started.
        const auto resourceStatus = KE_RES_AssignResources(pdo, req);
        if (resourceStatus != kernel::KernelError::Success) {
          KE_Print("PnP: assigning resources for {} failed: {}", hwid,
                   kernel::make_error_code(resourceStatus).message());
          pdo->pnpState = resourceStatus == kernel::KernelError::ResourceConflict
                              ? kernel::KPnpState::StoppedConflicting
                              : kernel::KPnpState::Stopped;
        } else {
          pdo->pnpState = kernel::KPnpState::Starting;

          // Start Device. The event/dispatch layer was removed in favour of raw
          // method calls (see note in pnp.hpp); StartDevice is invoked directly.
          if (pdo->driverObject && pdo->driverObject->StartDevice) {
            pdo->driverObject->StartDevice(pdo);
          }

          pdo->pnpState = kernel::KPnpState::Started;

          // AddDevice may have stacked a function device object (FDO) on top
          // of the PDO; StartDevice actually starts that top-of-stack device,
          // so its own pnpState must be marked Started too (otherwise it is
          // never flagged as started and the class-trigger warns spuriously).
          const auto &fdo = pdo->attachedDevice ? pdo->attachedDevice : pdo;
          fdo->pnpState = kernel::KPnpState::Started;

          // If the driver attached a Bus-type FDO, auto-enumerate its children.
          if (pdo->attachedDevice && pdo->attachedDevice->deviceType == kernel::device_type::Bus) {
            const auto enumerateStatus = KE_PNP_EnumerateBus(pdo->attachedDevice.As<kernel::KDeviceBus>());
            if (enumerateStatus != kernel::KernelError::Success) {
              KE_Print("PnP child enumeration failed for {}: {}", hwid,
                       kernel::make_error_code(enumerateStatus).message());
            }
          }

          // Broadcast that this driver/device has started (things like the GFX subsystem want to execute things after)
          TriggerDeviceStateChanged(fdo);

          // Class-trigger: attach any driver that registered interest in the
          // (just-started) function device object's type as an upper filter.
          TriggerDeviceInterest(fdo);
        }
      } else {
        KE_Print("PnP: AddDevice failed for {}: {}", hwid,
                 kernel::make_error_code(addStatus).message());
      }
    }

    // The requirements list is only needed for the reservation above; free it
    // once assignment is done (mirrors the ScatterGatherList lifecycle).
    KE_RES_FreeRequirementsList(req);
  } else {
    KE_Print("PnP: No driver for {}; continuing with class triggers", hwid);
    pdo->pnpState = kernel::KPnpState::Started;
    TriggerDeviceInterest(pdo);
  }
}


kernel::KernelError KE_PNP_EnumerateBus(const kernel::KDevicePtr<kernel::KDeviceBus> &busDevice) noexcept {
  if (!busDevice) return kernel::KernelError::InvalidArgument;
  if (!busDevice->driverObject) return kernel::KernelError::DeviceNotReady;

  // 1. Trigger hardware enumeration
  if (busDevice->driverObject->EnumerateDevices) {
    busDevice->driverObject->EnumerateDevices(busDevice);
  }

  // 2. Query discovered children
  if (busDevice->driverObject->GetChildCount && busDevice->driverObject->GetChild) {
    const size_t num_children = busDevice->driverObject->GetChildCount(busDevice);
    for (size_t i = 0; i < num_children; i++) {
      if (const auto child = busDevice->driverObject->GetChild(busDevice, i)) {
        const auto status = KE_PNP_ReportNewDevice(busDevice, child);
        if (status != kernel::KernelError::Success) return status;
      }
    }
  }
  return kernel::KernelError::Success;
}
