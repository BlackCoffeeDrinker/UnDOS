
#include <kernel/dma.hpp>
#include <kernel/kobject/KPhysicalDeviceObject.hpp>
#include <kernel/kobject/KDriverObject.hpp>

UNDOS_KERNEL_API kernel::DmaOperation* KE_DMA_GetAdapter(const kernel::KObjectPtr<kernel::KPhysicalDeviceObject> & device) {
  if (!device) return nullptr;

  // In Windows-style PnP, the request for a DMA adapter is typically handled by the bus driver.
  // We check the driver object associated with this PDO (which could be the function driver or the bus driver depending on binding)
  if (device->driverObject && device->driverObject->GetDmaAdapter) {
    if (auto* adapter = device->driverObject->GetDmaAdapter(device)) {
      return adapter;
    }
  }

  // If the current device/driver doesn't provide it, we recurse to the lower device (the bus).
  if (device->lowerDevice) {
    return KE_DMA_GetAdapter(device->lowerDevice);
  }

  return nullptr;
}
