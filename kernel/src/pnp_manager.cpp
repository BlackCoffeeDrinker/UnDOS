#include "event_dispatcher.hpp"
#include "object_manager.hpp"
#include "pnp_manager.hpp"

#include <Kernel.hpp>

static kernel::KObjectPtr<kernel::KDriverObject> s_registeredDrivers[32];
static size_t s_registeredDriverCount = 0;

static kernel::KObjectPtr<kernel::KDriverObject> FindDriverForDevice(const kernel::KObjectPtr<kernel::KDeviceObject>& device) {
  (void) device;
  // Match any device to the first registered driver (for testing)
  if (s_registeredDriverCount > 0) {
    for (size_t i = 0; i < s_registeredDriverCount; i++) {
      if (s_registeredDrivers[i]->name == "Sample") {
        return s_registeredDrivers[i];
      }
    }
  }
  return nullptr;
}

void KE_PNP_Init() {
  // PnP Manager initialization
}

void KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject>& driver) {
  if (s_registeredDriverCount < 32) {
    s_registeredDrivers[s_registeredDriverCount++] = driver;
  }
}

void KE_PNP_ReportNewDevice(const kernel::KObjectPtr<kernel::KDeviceObject>& parent, const kernel::KObjectPtr<kernel::KDeviceObject>& pdo) {
  (void) parent;
  if (!pdo) return;

  // 1. Add to \Device directory
  if (const auto devDir = KE_OB_LookupObject("\\Device")) {
    KE_OB_InsertObject(devDir.As<kernel::KDirectoryObject>(), pdo);
  }

  // 2. Matching and Attachment
  if (const auto driver = FindDriverForDevice(pdo)) {
    pdo->driverObject = driver;
    early_print_fmt("PnP: Attached driver {} to device {}\n", driver->name.data(), pdo->name.data());

    // Notify driver about the new device
    constexpr kernel::KPnpEvent startEvent(kernel::PnpMinorFunction::StartDevice);
    KE_PNP_DispatchEvent(pdo, startEvent);
  }
}

void KE_PNP_DispatchEvent(const kernel::KObjectPtr<kernel::KDeviceObject>& device, const kernel::KEvent &event) {
  KE_EVENT_DispatchToDevice(device, event);
}

void KE_PNP_EnumerateBus(const kernel::KObjectPtr<kernel::KDeviceObject>& busDevice) {
  if (!busDevice) return;
  constexpr kernel::KPnpEvent enumEvent(kernel::PnpMinorFunction::QueryDeviceRelations);
  KE_PNP_DispatchEvent(busDevice, enumEvent);
}
