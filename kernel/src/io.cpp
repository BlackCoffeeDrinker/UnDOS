
#include "stdkrn.hpp"
#include <kernel/dispatch.hpp>

namespace {
bool setupCommonKDevice(const kernel::KDevicePtr<kernel::KDevice> &device,
                        const kernel::KObjectPtr<kernel::KDriverObject> &driver,
                        size_t deviceExtensionSize,
                        const kstd::string_view &deviceName, CreateDeviceFlags flags) {

  device->driverObject = driver;

  if (deviceExtensionSize > 0) {
    device->deviceExtension = kernel::DataBuffer::Alloc(deviceExtensionSize);
    if (!device->deviceExtension) {
      return false;
    }
  } else {
    device->deviceExtension = nullptr;
  }

  // Only named devices are inserted into the \Device directory. Unnamed
  // devices (empty deviceName) live purely in the device stack / devnode
  // tree, exactly like Windows PDOs/FDOs, and cannot collide with siblings.
  if (!deviceName.empty()) {
    kstd::static_string<64> deviceNameStr;
    
    if (flags & CreateDeviceFlags::MultipleAllowed) {
      const auto num = KE_OB_MakeUniqueName(KE_OB_GetDeviceDirectory(), deviceName);
      kstd::format(deviceNameStr, "{}{}", deviceName, num);
    } else {
      kstd::format(deviceNameStr, "{}", deviceName);
    }
    
    const auto deviceObj = kernel::CreateKObject<kernel::KDeviceObject>(deviceNameStr);
    deviceObj->device = device;
    device->deviceObject = deviceObj;

    if (!KE_OB_InsertObject(KE_OB_GetDeviceDirectory(), deviceObj)) {
      early_print_fmt("KE_OB_InsertObject failed for device {}\r\n", deviceName);
      // Collision ?
      return false;
    }
  }

  return true;
}
}// namespace

UNDOS_KERNEL_API_DEF kernel::KDevicePtr<kernel::KDevice> KE_IO_CreateDevice(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    size_t deviceExtensionSize,
    const kstd::string_view &baseDeviceName,
    kernel::DeviceType deviceType, CreateDeviceFlags flags) noexcept {

  if (auto device = KE_CreateKPtrObject<kernel::KDevice>(driver)) {
    setupCommonKDevice(device, driver, deviceExtensionSize, baseDeviceName, flags);
    device->deviceType = deviceType;
    return device;
  }

  return nullptr;
}

UNDOS_KERNEL_API_DEF kernel::KDevicePtr<kernel::KDeviceBus> KE_IO_CreateBus(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    size_t deviceExtensionSize,
    const kstd::string_view &busName, CreateDeviceFlags flags) noexcept {

  if (auto device = KE_CreateKPtrObject<kernel::KDeviceBus>(driver)) {
    setupCommonKDevice(device, driver, deviceExtensionSize, busName, flags);
    device->deviceType = kernel::device_type::Bus;
    return device;
  }

  return nullptr;
}

UNDOS_KERNEL_API_DEF kernel::KDevicePtr<kernel::KDevice> KE_IO_AttachFilterDevice(
    const kernel::KDevicePtr<kernel::KDevice> &sourceDevice,
    const kernel::KDevicePtr<kernel::KDevice> &targetDevice) noexcept {
  if (!sourceDevice || !targetDevice)
    return nullptr;

  // Walk to the current top of the target's device stack.
  auto top = targetDevice;
  while (top->attachedDevice) {
    top = top->attachedDevice;
  }

  top->attachedDevice = sourceDevice;
  sourceDevice->lowerDevice = top;

  return top;
}

UNDOS_KERNEL_API_DEF kernel::KDevicePtr<kernel::KDevice> KE_IO_AttachChildDevice(const kernel::KDevicePtr<kernel::KDeviceBus> &bus,
                                                                                 kernel::KDevicePtr<kernel::KDevice> child) noexcept {

  // Sanity checks
  if (child->parentBus != nullptr) [[unlikely]] {
    early_print_fmt("KE_IO_AttachChildDevice: Device {} already has a parent bus ({}).\r\n", child->Name(), child->parentBus->Name());
    return nullptr;
  }

  early_print_fmt("KE_IO_AttachChildDevice: Bus = {} Child = {}\r\n",
                  bus->Name(), child->Name());

  bus->children.push_back(child);
  child->parentBus = bus;

  return child;
}

UNDOS_KERNEL_API_DEF kernel::IoStatus KE_IO_ReadDevice(
    const kernel::KDevicePtr<kernel::KDevice> &device,
    uint64_t offset,
    kstd::span<uint8_t> buffer,
    size_t &transferred) noexcept {

  transferred = 0;

  if (!device || !buffer.data()) return kernel::IoStatus::InvalidParameter;
  if (buffer.empty()) return kernel::IoStatus::Success;

  if (!device->driverObject || !device->driverObject->Read) {
    return kernel::IoStatus::Unsupported;
  }
  return device->driverObject->Read(device, offset, buffer, transferred);
}

UNDOS_KERNEL_API_DEF kernel::IoStatus KE_IO_WriteDevice(
    const kernel::KDevicePtr<kernel::KDevice> &device,
    uint64_t offset,
    kstd::span<const uint8_t> buffer,
    size_t &transferred) noexcept {
  transferred = 0;
  if (!device || !buffer.data()) return kernel::IoStatus::InvalidParameter;
  if (buffer.empty()) return kernel::IoStatus::Success;
  if (!device->driverObject || !device->driverObject->Write) {
    return kernel::IoStatus::Unsupported;
  }
  return device->driverObject->Write(device, offset, buffer, transferred);
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> KE_IO_ConnectInterrupt(
    const kernel::KDevicePtr<kernel::KDevice> & /*device*/,
    uint32_t irq,
    kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)> routine,
    void *context) noexcept {
  if (!routine) return nullptr;

  auto isr = kernel::CreateAnonymousKObject<kernel::KInterruptServiceRoutineObject>();
  if (!isr) return nullptr;

  isr->service_routine = routine;
  isr->service_context = context;
  isr->irql = 0;

  if (!KE_DISPATCH_RegisterHandler(irq, isr)) {
    return nullptr;
  }

  if (!HAL_INTR_ConnectInterrupt(irq, isr)) {
    KE_DISPATCH_UnregisterHandler(irq);
    return nullptr;
  }

  return isr;
}

UNDOS_KERNEL_API_DEF void KE_IO_DisconnectInterrupt(uint32_t irq) noexcept {
  KE_DISPATCH_UnregisterHandler(irq);
  HAL_INTR_DisconnectInterrupt(irq);
}
