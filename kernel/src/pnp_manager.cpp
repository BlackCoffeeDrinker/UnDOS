#include "event_dispatcher.hpp"
#include "object_manager.hpp"
#include <kernel/hal_interface.hpp>
#include <kernel/pnp.hpp>

static kernel::KObjectPtr<kernel::KDriverObject> s_registeredDrivers[32];
static size_t s_registeredDriverCount = 0;

static kernel::KObjectPtr<kernel::KDriverObject> FindDriverForDevice(kernel::KObjectPtr<kernel::KDeviceObject> device) {
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

void Ke_PNP_Init() {
  // PnP Manager initialization
}

void Ke_PNP_RegisterDriver(kernel::KObjectPtr<kernel::KDriverObject> driver) {
  if (s_registeredDriverCount < 32) {
    s_registeredDrivers[s_registeredDriverCount++] = driver;
  }
}

void Ke_PNP_ReportNewDevice(kernel::KObjectPtr<kernel::KDeviceObject> parent, kernel::KObjectPtr<kernel::KDeviceObject> pdo) {
  (void) parent;
  if (!pdo) return;

  // 1. Add to \Device directory
  if (const auto devDir = KE_Ob_LookupObject("\\Device")) {
    KE_Ob_InsertObject(static_cast<kernel::KDirectoryObject *>(devDir.get()), pdo.get());
  }

  // 2. Matching and Attachment
  if (const auto driver = FindDriverForDevice(pdo)) {
    pdo->driverObject = driver;
    early_print_fmt("PnP: Attached driver {} to device {}\n", driver->name.data(), pdo->name.data());

    // Notify driver about the new device
    constexpr kernel::KPnpEvent startEvent(kernel::PnpMinorFunction::StartDevice);
    Ke_PNP_DispatchEvent(pdo, startEvent);
  }
}

void Ke_PNP_DispatchEvent(kernel::KObjectPtr<kernel::KDeviceObject> device, const kernel::KEvent &event) {
  kernel::EventDispatcher::DispatchToDevice(device, event);
}

void Ke_PNP_EnumerateBus(kernel::KObjectPtr<kernel::KDeviceObject> busDevice) {
  if (!busDevice) return;
  constexpr kernel::KPnpEvent enumEvent(kernel::PnpMinorFunction::QueryDeviceRelations);
  Ke_PNP_DispatchEvent(busDevice, enumEvent);
}
