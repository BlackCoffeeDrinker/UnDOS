#include "pnp_manager.hpp"
#include "driver_loader.hpp"
#include "event_broadcaster.hpp"
#include "stdkrn.hpp"

#include <kernel/configuration_manager.hpp>
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
  // Parked devices only: the CriticalDeviceDatabase service resolved when
  // the device was parked (empty when none), so driver registration needs
  // no registry lookup.
  kstd::string service;
  // Class triggers already ran on the bare PDO; they are never run twice.
  bool triggersRun = false;
};

kernel::Mutex s_queueLock;
kstd::list<PnpWorkItem> s_workQueue;
// Devices processed while no driver matched them (guarded by s_queueLock).
// A driver that registers later re-queues the ones it matches, as Windows
// 2000 re-examines unbound devices when a new driver arrives.
kstd::list<PnpWorkItem> s_unboundDevices;
// Serialises "match a device or park it as unbound" (ProcessDevice) against
// "register a driver and re-examine parked devices" (KE_PNP_RegisterDriver),
// so a driver arriving between a failed match and the park is never missed.
// Lock order: s_bindLock, then s_registryLock / s_queueLock.
kernel::Mutex s_bindLock;
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

// Windows 2000 CriticalDeviceDatabase analogue: maps a hardware ID such as
// PCI\VEN_1013&DEV_00B8 to a driver name through
// LocalMachine/CriticalDeviceDatabase/<hwid>/Service. '\' and '&' in the
// hardware ID are stored as '#' in the key name, and letters are uppercased
// (bus enumerators may format hex digits in either case).
// When the mapped driver is not registered, its service name is returned in
// `serviceOut` so the caller can demand-load it.
static kernel::KObjectPtr<kernel::KDriverObject> FindDriverInCriticalDeviceDatabase(const kstd::string_view &hwid,
                                                                                    kstd::string *serviceOut) {
  kstd::string keyName;
  keyName.resize(hwid.size());
  for (size_t i = 0; i < hwid.size(); ++i) {
    const char c = hwid[i];
    keyName[i] = (c == '\\' || c == '&') ? '#' : (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A')
                                                                        : c;
  }

  const auto localMachine = KE_CM_OpenRoot("LocalMachine");
  if (!localMachine) {
    KE_Print("PnP: CriticalDeviceDatabase: cannot open LocalMachine: {}", localMachine.error().message());
    return nullptr;
  }
  const auto cddb = KE_CM_OpenKey(*localMachine, "CriticalDeviceDatabase");
  if (!cddb) {
    KE_Print("PnP: no CriticalDeviceDatabase key: {}", cddb.error().message());
    return nullptr;
  }
  const auto entry = KE_CM_OpenKey(*cddb, kstd::string_view(keyName.data(), keyName.size()));
  if (!entry)
    return nullptr;// most hardware IDs have no entry
  const auto serviceKey = KE_CM_OpenKey(*entry, "Service");
  if (!serviceKey) {
    KE_Print("PnP: CriticalDeviceDatabase entry {} has no Service", hwid);
    return nullptr;
  }
  const auto size = KE_CM_GetKeyValueStringSize(*serviceKey);
  if (!size) {
    KE_Print("PnP: CriticalDeviceDatabase entry {} has an invalid Service: {}", hwid, size.error().message());
    return nullptr;
  }
  kstd::string service;
  service.resize(*size);
  if (const auto read = KE_CM_GetKeyValueString(*serviceKey, kstd::span(service.data(), service.size())); !read) {
    KE_Print("PnP: CriticalDeviceDatabase entry {} has an unreadable Service: {}", hwid, read.error().message());
    return nullptr;
  }
  while (!service.empty() && service[service.size() - 1] == '\0') service.resize(service.size() - 1);

  const kstd::string_view serviceName(service.data(), service.size());
  const auto driver = FindDriverForHardwareId(serviceName);
  KE_Print("PnP: CriticalDeviceDatabase maps {} -> {}{}", hwid, serviceName,
           driver ? "" : " (driver not registered)");
  if (!driver && serviceOut)
    *serviceOut = service;
  return driver;
}

static void ProcessDevice(const PnpWorkItem &item);
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
      ProcessDevice(item);

      kstd::unique_lock guard(s_queueLock);
      if (s_workQueue.empty()) {
        s_workerBusy = false;
      }
    } else {
      const auto waitStatus = KE_SCHED_WaitForHeader(s_workAvailable, kernel::WAIT_INFINITE);
      if (waitStatus) {
        KE_Print("PnP worker wait failed: {}", waitStatus.message());
        KE_SCHED_Yield();
      }
    }
  }
}

void init() {
  // Create the kernel-owned root enumerator (\Device\Root). It has no driver
  // object; its child PDOs are tracked in the PnpRootContext extension.
  auto root = KE_IO_CreateBus(nullptr, 0, "ROOT");
  if (!root) {
    KE_Print("Failed to initialize PnP: {}", root.error().message());
    HAL_PLATFORM_Panic("Failed to initialize PnP", __FILE__, __LINE__);
  }
  s_rootDevice = *root;
  s_workAvailable.manualReset = false;
  s_workAvailable.signalled = false;
  auto worker = KE_SCHED_CreateThread(&PnPThread, nullptr, kernel::ThreadPriority::Idle);
  if (!worker) {
    KE_Print("Failed to initialize PnP: {}", worker.error().message());
    HAL_PLATFORM_Panic("Failed to initialize PnP", __FILE__, __LINE__);
  }
  s_pnpThread = *worker;
}

void EnumerateRootFromHive() {
  // Windows 2000-style Enum\Root: every subkey of LocalMachine/Enum/Root is
  // one ROOT child whose HardwareId names the driver to bind. Subkeys are
  // reported in index order, and the single PnP worker binds them in order.
  const auto localMachine = KE_CM_OpenRoot("LocalMachine");
  if (!localMachine) {
    KE_Print("PnP: ROOT enumeration: failed to open LocalMachine: {}", localMachine.error().message());
    return;
  }
  const auto enumKey = KE_CM_OpenKey(*localMachine, "Enum");
  if (!enumKey) {
    KE_Print("PnP: ROOT enumeration: no Enum key: {}", enumKey.error().message());
    return;
  }
  const auto rootKey = KE_CM_OpenKey(*enumKey, "Root");
  if (!rootKey) {
    KE_Print("PnP: ROOT enumeration: no Enum/Root key: {}", rootKey.error().message());
    return;
  }

  for (uint32_t index = 0;; ++index) {
    const auto childEntry = KE_CM_EnumKey(*rootKey, index);
    if (!childEntry) {
      KE_Print("PnP: ROOT enumeration failed: {}", childEntry.error().message());
      break;
    }
    if (!childEntry->available)
      break;
    const auto childName = childEntry->name;

    const auto childKey = KE_CM_OpenKey(*rootKey, childName);
    if (!childKey) {
      KE_Print("PnP: ROOT entry '{}' cannot be opened: {}", childName, childKey.error().message());
      continue;
    }
    const auto hwidKey = KE_CM_OpenKey(*childKey, "HardwareId");
    if (!hwidKey) {
      KE_Print("PnP: ROOT entry '{}' has no HardwareId, skipping", childName);
      continue;
    }
    const auto hwidSize = KE_CM_GetKeyValueStringSize(*hwidKey);
    if (!hwidSize) {
      KE_Print("PnP: ROOT entry '{}' has invalid HardwareId: {}", childName, hwidSize.error().message());
      continue;
    }
    kstd::string hwid;
    hwid.resize(*hwidSize);
    if (const auto read = KE_CM_GetKeyValueString(*hwidKey, kstd::span(hwid.data(), hwid.size())); !read) {
      KE_Print("PnP: ROOT entry '{}' failed to read HardwareId: {}", childName, read.error().message());
      continue;
    }
    // Drop any trailing NULs the value size may include.
    while (!hwid.empty() && hwid[hwid.size() - 1] == '\0') hwid.resize(hwid.size() - 1);
    if (hwid.empty()) {
      KE_Print("PnP: ROOT entry '{}' has an empty HardwareId, skipping", childName);
      continue;
    }

    const kstd::string_view id(hwid.data(), hwid.size());
    KE_Print("PnP: ROOT\\{} -> {}", childName, id);
    if (const auto device = KE_PNP_ReportRootDevice(id); !device) {
      KE_Print("PnP: failed to report ROOT entry '{}': {}", childName, device.error().message());
    }
  }
}
}// namespace kernel::pnp

UNDOS_KERNEL_API_DEF kernel::KDevicePtr<kernel::KDeviceBus> KE_PNP_GetRootDevice() noexcept {
  return s_rootDevice;
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KDevicePtr<kernel::KDevice>, kstd::error_code>
KE_PNP_ReportRootDevice(const kstd::string_view &hardwareId) noexcept {
  // Create an unnamed PDO advertising the given hardware id.
  auto pdo = KE_IO_CreateDevice(
      nullptr,
      0,
      {},
      kernel::device_type::Unknown);
  if (!pdo)
    return kstd::unexpected(pdo.error());
  pdo.value()->hardwareId = hardwareId;

  const auto status = KE_PNP_ReportNewDevice(s_rootDevice, *pdo);
  if (status)
    return kstd::unexpected(status);

  return *pdo;
}

// The hardware ID list: one ID, or several separated by '\n', most
// specific first (e.g. PCI ...&SUBSYS_..&REV_.., then ...&DEV_.., then CC_..).
static kstd::string_view HardwareIdOf(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  if (!pdo->hardwareId.empty())
    return pdo->hardwareId;
  if (pdo->driverObject && pdo->driverObject->GetHardwareId)
    return pdo->driverObject->GetHardwareId(pdo);
  return {};
}

// Calls fn(id) for each ID in a hardware ID list until it returns true.
template<typename Fn>
static bool ForEachHardwareId(const kstd::string_view list, Fn &&fn) {
  size_t start = 0;
  while (start < list.size()) {
    size_t end = start;
    while (end < list.size() && list[end] != '\n') ++end;
    if (end > start && fn(list.substr(start, end - start)))
      return true;
    start = end + 1;
  }
  return false;
}

// The most specific ID, for log messages.
static kstd::string_view PrimaryHardwareId(const kstd::string_view list) {
  size_t end = 0;
  while (end < list.size() && list[end] != '\n') ++end;
  return list.substr(0, end);
}

kstd::error_code KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept {
  if (!driver)
    return kernel::KernelError::InvalidArgument;
  kstd::unique_lock bind(s_bindLock);
  {
    kstd::unique_lock guard(s_registryLock);
    for (const auto &registered: s_registeredDrivers) {
      if (registered == driver)
        return kernel::KernelError::AlreadyExists;
    }
    s_registeredDrivers.push_back(driver);
  }

  // Re-queue unbound devices this driver now matches, by name or by the
  // CriticalDeviceDatabase service cached when they were parked.
  bool requeued = false;
  {
    kstd::unique_lock guard(s_queueLock);
    kstd::list<PnpWorkItem> stillParked;
    for (auto &item: s_unboundDevices) {
      const auto hwid = HardwareIdOf(item.pdo);
      const bool match = (!item.service.empty() && driver->name == kstd::string_view(item.service.data(), item.service.size())) ||
                         ForEachHardwareId(hwid, [&](const kstd::string_view id) { return driver->name == id; });
      if (match) {
        KE_Print("PnP: driver {} arrived, re-examining {}", driver->name, PrimaryHardwareId(hwid));
        s_workQueue.push_back(kstd::move(item));
        requeued = true;
      } else {
        stillParked.push_back(kstd::move(item));
      }
    }
    s_unboundDevices = kstd::move(stillParked);
  }
  if (requeued)
    KE_EVENT_Signal(s_workAvailable, false);
  return {};
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_PNP_RegisterDeviceInterest(
    kernel::DeviceType type,
    const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept {
  if (!driver || type == kernel::device_type::Unknown)
    return kernel::KernelError::InvalidArgument;
  if (!driver->AddDevice) {
    KE_Print("KE_PNP_RegisterDeviceInterest: ERROR Driver {} does not implement AddDevice, cannot register interest", driver->name);
    return kernel::KernelError::Unsupported;
  }

  kstd::unique_lock guard(s_registryLock);
  s_deviceInterests.emplace_back(type, driver);
  return {};
}

static void TriggerDeviceStateChanged(const kernel::KDevicePtr<kernel::KDevice> &fdo) {
  if (fdo && fdo->pnpState == kernel::KPnpState::Started) {
    const auto status =
        kernel::event::broadcast(fdo, kernel::event::make_event_code(KE_PNP_EventCategory(), kernel::pnp::DeviceEvent::Started));
    if (status) {
      KE_Print("PnP started event broadcast failed: {}", status.message());
    }
  }
}

// Attaches every driver that registered interest in the given FDO's device type
// as an upper filter (AddDevice) and starts it. This is the class-trigger that
// loads a secondary driver (e.g. volmgr) when a device type appears.
static void TriggerDeviceInterest(const kernel::KDevicePtr<kernel::KDevice> &fdo) {
  if (!fdo)
    return;

  // Bare PDOs with no function driver stay NotStarted (see ProcessDevice).
  if (fdo->pnpState != kernel::KPnpState::Started && fdo->pnpState != kernel::KPnpState::NotStarted) {
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
    if (!addStatus) {
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
               addStatus.message());
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

UNDOS_KERNEL_API_DEF kstd::error_code KE_PNP_ReportNewDevice(
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
    if (status)
      return status;
  }

  {
    kstd::unique_lock guard(s_queueLock);
    s_workQueue.emplace_back(PnpWorkItem{.parent = parent, .pdo = pdo, .service = {}, .triggersRun = false});
  }

  // Sticky signal: safe even if the worker has not reached its wait yet, and
  // safe before init() created the thread at all.
  KE_EVENT_Signal(s_workAvailable, false);
  return {};
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

static void ProcessDevice(const PnpWorkItem &item) {
  const auto &parent = item.parent;
  const auto &pdo = item.pdo;
  // Identification: prefer the explicit hardwareId field (set on root/legacy
  // PDOs that have no driver), otherwise fall back to the enumerating driver's
  // GetHardwareId callback.
  const kstd::string_view hwids = HardwareIdOf(pdo);
  const kstd::string_view hwid = PrimaryHardwareId(hwids);
  if (hwid.empty()) {
    KE_Print("KE_PNP_ReportNewDevice: PDO {} has no hardware ID.", pdo->Name());
    return;
  }

  // Already bound (e.g. reported again by a bus rescan): nothing to do. A
  // re-queued parked device may read Started only because a class-trigger
  // filter started on its bare PDO; it still has no function driver.
  if (pdo->pnpState != kernel::KPnpState::NotStarted && !item.triggersRun) {
    return;
  }

  // Matching, and parking when nothing matches, happen atomically with
  // respect to driver registration (see s_bindLock).
  kernel::KObjectPtr<kernel::KDriverObject> driver;
  kstd::string demandService;
  {
    kstd::unique_lock bind(s_bindLock);
    // IDs are tried most specific first: by driver name, then through the
    // CriticalDeviceDatabase.
    ForEachHardwareId(hwids, [&](const kstd::string_view id) {
      driver = FindDriverForHardwareId(id);
      return static_cast<bool>(driver);
    });
    if (!driver) {
      ForEachHardwareId(hwids, [&](const kstd::string_view id) {
        driver = FindDriverInCriticalDeviceDatabase(id, &demandService);
        return driver || !demandService.empty();
      });
    }
    if (!driver) {
      // Parked devices stay NotStarted; a matching driver re-queues them.
      // Class triggers are deferred while a demand load is pending, so they
      // run once, on the bound device.
      kstd::unique_lock guard(s_queueLock);
      s_unboundDevices.push_back(PnpWorkItem{.parent = parent,
                                             .pdo = pdo,
                                             .service = demandService,
                                             .triggersRun = item.triggersRun || demandService.empty()});
    }
  }
  if (driver) {
    // Capture the child's resource requirements from its *enumerating* driver
    // BEFORE AddDevice rebinds pdo->driverObject to the function driver. The
    // enumerator (e.g. the ISA bus) is the one that knows the controller's
    // fixed ports/IRQ.
    kernel::IoResourceRequirementsList *req = nullptr;
    if (pdo->driverObject && pdo->driverObject->QueryResourceRequirements) {
      const auto requirements = pdo->driverObject->QueryResourceRequirements(pdo);
      if (!requirements) {
        KE_Print("PnP: failed to query resource requirements for {}: {}", hwid,
                 requirements.error().message());
        return;
      }
      req = *requirements;
    }

    // Binding
    if (driver->AddDevice) {
      const auto addStatus = driver->AddDevice(driver, pdo);
      if (!addStatus) {
        pdo->driverObject = driver;
        KE_Print("PnP: Bound driver {} to device {}", driver->name, hwid);

        // Reserve the device's fixed resources before starting it. A conflict
        // (an overlapping range already held by another device) leaves the
        // device un-started.
        const auto resourceStatus = KE_RES_AssignResources(pdo, req);
        if (resourceStatus) {
          KE_Print("PnP: assigning resources for {} failed: {}", hwid,
                   resourceStatus.message());
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
            if (enumerateStatus) {
              KE_Print("PnP child enumeration failed for {}: {}", hwid,
                       enumerateStatus.message());
            }
          }

          // Broadcast that this driver/device has started (things like the GFX subsystem want to execute things after)
          TriggerDeviceStateChanged(fdo);

          // Class-trigger: attach any driver that registered interest in the
          // (just-started) function device object's type as an upper filter.
          // Skipped when they already ran on the bare, parked PDO.
          if (!item.triggersRun)
            TriggerDeviceInterest(fdo);
        }
      } else {
        KE_Print("PnP: AddDevice failed for {}: {}", hwid,
                 addStatus.message());
      }
    }

    // The requirements list is only needed for the reservation above; free it
    // once assignment is done (mirrors the ScatterGatherList lifecycle).
    KE_RES_FreeRequirementsList(req);
  } else {
    if (!demandService.empty()) {
      KE_Print("PnP: No driver for {} yet; demand-loading {}", hwid,
               kstd::string_view(demandService.data(), demandService.size()));
      // Outside s_bindLock: the load registers the driver, which takes that
      // lock and re-queues this parked device.
      kernel::driver::RequestDemandLoad(kstd::string_view(demandService.data(), demandService.size()));
    } else if (!item.triggersRun) {
      KE_Print("PnP: No driver for {}; continuing with class triggers", hwid);
      TriggerDeviceInterest(pdo);
    }
  }
}


UNDOS_KERNEL_API_DEF kstd::error_code KE_PNP_EnumerateBus(const kernel::KDevicePtr<kernel::KDeviceBus> &busDevice) noexcept {
  if (!busDevice)
    return kernel::KernelError::InvalidArgument;
  if (!busDevice->driverObject)
    return kernel::KernelError::DeviceNotReady;

  // 1. Trigger hardware enumeration
  if (busDevice->driverObject->EnumerateDevices) {
    busDevice->driverObject->EnumerateDevices(busDevice);
  }

  // 2. Query discovered children
  if (busDevice->driverObject->GetChildCount && busDevice->driverObject->GetChild) {
    const size_t num_children = busDevice->driverObject->GetChildCount(busDevice);
    for (size_t i = 0; i < num_children; i++) {
      if (const auto child = busDevice->driverObject->GetChild(busDevice, i)) {
        // A rescan returns children reported earlier; only new ones are reported.
        if (child->parentBus == busDevice)
          continue;
        const auto status = KE_PNP_ReportNewDevice(busDevice, child);
        if (status)
          return status;
      }
    }
  }
  return {};
}
