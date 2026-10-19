
#include <kernel/dma.hpp>
#include <kernel/kobject/KDeviceObject.hpp>
#include <kernel/kobject/KDriverObject.hpp>

UNDOS_KERNEL_API_DEF kstd::expected<kernel::DmaOperation *, kernel::KernelError> KE_DMA_GetAdapter(const kernel::KDevicePtr<kernel::KDevice> &device, const kernel::DmaDescription &description) noexcept {
  if (!device) return kstd::unexpected(kernel::KernelError::InvalidArgument);

  // In Windows-style PnP, the request for a DMA adapter is typically handled by the bus driver.
  // We check the driver object associated with this device
  if (device->driverObject && device->driverObject->GetDmaAdapter) {
    auto adapter = device->driverObject->GetDmaAdapter(device, description);
    if (adapter) return adapter;
    if (adapter.error() != kernel::KernelError::NotFound && adapter.error() != kernel::KernelError::Unsupported) return adapter;
  }

  // If the current device/driver doesn't provide it, we recurse to the lower device (the stack).
  if (device->lowerDevice) {
    return KE_DMA_GetAdapter(device->lowerDevice, description);
  }

  // If we reached the bottom of the stack, recurse to the parent bus.
  if (device->parentBus) {
    return KE_DMA_GetAdapter(device->parentBus, description);
  }

  return kstd::unexpected(kernel::KernelError::NotFound);
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_DMA_AllocateAdapterChannel(kernel::DmaOperation *self, const kernel::KDevicePtr<kernel::KDevice> &deviceObject, uint32_t numberOfMapRegisters, kernel::cfunc<kernel::DmaAction(const kernel::KDevicePtr<kernel::KDevice> &deviceObject, kernel::VirtualAddress context, kernel::VirtualAddress mapRegisterBase, kernel::VirtualAddress reserved)> executionRoutine, kernel::VirtualAddress context) noexcept {
  if (!self) return kernel::KernelError::InvalidArgument;
  if (!self->AllocateAdapterChannel) return kernel::KernelError::Unsupported;
  return self->AllocateAdapterChannel(self, deviceObject, numberOfMapRegisters, executionRoutine, context);
}

UNDOS_KERNEL_API_DEF void KE_DMA_FreeAdapterChannel(kernel::DmaOperation *self, kernel::VirtualAddress mapRegisterBase) noexcept {
  if (self && self->FreeAdapterChannel) self->FreeAdapterChannel(self, mapRegisterBase);
}

UNDOS_KERNEL_API_DEF void KE_DMA_FreeMapRegisters(kernel::DmaOperation *self, kernel::VirtualAddress mapRegisterBase, uint32_t numberOfMapRegisters) noexcept {
  if (self && self->FreeMapRegisters) self->FreeMapRegisters(self, mapRegisterBase, numberOfMapRegisters);
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::PhysicalAddress, kernel::KernelError> KE_DMA_MapTransfer(kernel::DmaOperation *self, kernel::VirtualAddress mapRegisterBase, kernel::VirtualAddress virtualAddress, size_t *length, kernel::DmaDirection direction) noexcept {
  if (!self) return kstd::unexpected(kernel::KernelError::InvalidArgument);
  if (!self->MapTransfer) return kstd::unexpected(kernel::KernelError::Unsupported);
  return self->MapTransfer(self, mapRegisterBase, virtualAddress, length, direction);
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_DMA_GetScatterGatherList(kernel::DmaOperation *self, const kernel::KDevicePtr<kernel::KDevice> &deviceObject, kernel::VirtualAddress virtualAddress, size_t length, kernel::DmaDirection direction, kernel::cfunc<void(const kernel::KDevicePtr<kernel::KDevice> &deviceObject, kernel::VirtualAddress context, kernel::ScatterGatherList *sgList)> executionRoutine, kernel::VirtualAddress context) noexcept {
  if (!self) return kernel::KernelError::InvalidArgument;
  if (!self->GetScatterGatherList) return kernel::KernelError::Unsupported;
  return self->GetScatterGatherList(self, deviceObject, virtualAddress, length, direction, executionRoutine, context);
}

UNDOS_KERNEL_API_DEF void KE_DMA_PutScatterGatherList(kernel::DmaOperation *self, kernel::ScatterGatherList *sgList, kernel::DmaDirection direction) noexcept {
  if (self && self->PutScatterGatherList) self->PutScatterGatherList(self, sgList, direction);
}
