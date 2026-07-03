#include "pnp_manager.hpp"
#include "event_dispatcher.hpp"
#include "object_manager.hpp"

#include "stdkrn.hpp"
#include <Kernel.hpp>

static kernel::KObjectPtr<kernel::KDriverObject> s_registeredDrivers[32];
static size_t s_registeredDriverCount = 0;

static kernel::KObjectPtr<kernel::KDriverObject> FindDriverForHardwareId(const kstd::string_view &hwid) {
  for (size_t i = 0; i < s_registeredDriverCount; i++) {
    if (s_registeredDrivers[i]->name == hwid) {
      return s_registeredDrivers[i];
    }
  }
  return nullptr;
}

void KE_PNP_Init() {
  // PnP Manager initialization
}

void KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject> &driver) {
  if (s_registeredDriverCount < 32) {
    s_registeredDrivers[s_registeredDriverCount++] = driver;
  }
}

void KE_PNP_ReportNewDevice(const kernel::KObjectPtr<kernel::KPhysicalDeviceObject> &parent, const kernel::KObjectPtr<kernel::KPhysicalDeviceObject> &pdo) {
  if (!pdo) return;
  pdo->lowerDevice = parent;

  // Identification
  if (pdo->driverObject && pdo->driverObject->GetHardwareId) {
    const auto hwid = pdo->driverObject->GetHardwareId(pdo);

    // Matching
    if (const auto driver = FindDriverForHardwareId(hwid)) {
      // Binding
      if (driver->AddDevice) {
        if (driver->AddDevice(driver, pdo)) {
          pdo->driverObject = driver;
          early_print_fmt("PnP: Bound driver {} to device {}\n", driver->name.data(), pdo->name.data());

          // Start Device
          constexpr kernel::KPnpEvent startEvent(kernel::PnpMinorFunction::StartDevice);
          KE_PNP_DispatchEvent(pdo, startEvent);
        }
      }
    }
  }
}

void KE_PNP_DispatchEvent(const kernel::KObjectPtr<kernel::KPhysicalDeviceObject> &device, const kernel::KEvent &event) {
  if (event.type == kernel::EventType::Pnp) {
    const auto &pnp = static_cast<const kernel::KPnpEvent &>(event);
    if (device->driverObject) {
      if (pnp.minorFunction == kernel::PnpMinorFunction::StartDevice && device->driverObject->StartDevice) {
        device->driverObject->StartDevice(device);
        return;
      }
    }
  }
  KE_EVENT_DispatchToDevice(device, event);
}

void KE_PNP_EnumerateBus(const kernel::KObjectPtr<kernel::KPhysicalDeviceObject> &busDevice) {
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
