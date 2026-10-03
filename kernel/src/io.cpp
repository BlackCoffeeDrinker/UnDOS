
#include "stdkrn.hpp"
#include <kernel/dispatch.hpp>
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/tty/tty.hpp>
#include <kernel/tty/tty_device.hpp>
#include <kernel/vfs.hpp>

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
      KE_Print("KE_OB_InsertObject failed for device {}", deviceName);
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
    KE_Print("KE_IO_AttachChildDevice: Device {} already has a parent bus ({}).", child->Name(), child->parentBus->Name());
    return nullptr;
  }


  bus->children.push_back(child);
  child->parentBus = bus;
  KE_Print("KE_IO_AttachChildDevice: Bus = {} Child = {}", bus->Name(), child->Name());

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

UNDOS_KERNEL_API_DEF kernel::IoStatus KE_IO_ControlDevice(
    const kernel::KDevicePtr<kernel::KDevice> &device,
    uint32_t code,
    kstd::span<const uint8_t> input,
    kstd::span<uint8_t> output,
    size_t &transferred) noexcept {
  transferred = 0;
  if (!device) return kernel::IoStatus::InvalidParameter;
  if (!device->driverObject || !device->driverObject->DeviceControl) {
    return kernel::IoStatus::Unsupported;
  }
  return device->driverObject->DeviceControl(device, code, input, output, transferred);
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

namespace kernel {

KIOObject::~KIOObject() {
  if (!isClosed) {
    // Close ops may wrap `this` in a temporary KObjectPtr; keep it from re-freeing.
    KE_OB_Retain(this);
    (void) Close();
  }
}

IoStatus KIOObject::Read(kstd::span<uint8_t> buffer, size_t &transferred) {
  transferred = 0;
  if (isClosed) return IoStatus::DeviceError;
  if (!ops || !ops->Read) return IoStatus::Unsupported;
  return ops->Read(*this, buffer, transferred);
}

IoStatus KIOObject::Write(kstd::span<const uint8_t> buffer, size_t &transferred) {
  transferred = 0;
  if (isClosed) return IoStatus::DeviceError;
  if (!ops || !ops->Write) return IoStatus::Unsupported;
  return ops->Write(*this, buffer, transferred);
}

IoStatus KIOObject::Control(uint32_t code, kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred) {
  transferred = 0;
  if (isClosed) return IoStatus::DeviceError;
  if (!ops || !ops->Control) return IoStatus::Unsupported;
  return ops->Control(*this, code, input, output, transferred);
}

IoStatus KIOObject::Close() {
  if (isClosed) return IoStatus::Success;
  if (!ops || !ops->Close) {
    isClosed = true;
    return IoStatus::Success;
  }
  return ops->Close(*this);
}

namespace {
IoStatus TtySinkRead(KIOObject &io, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return IoStatus::InvalidParameter;
  KE_TTY_PumpDevice(io.vtty);
  transferred = io.vtty->sink_read(buffer.data(), buffer.size());
  io.offset += transferred;
  return IoStatus::Success;
}

IoStatus TtySinkWrite(KIOObject &io, kstd::span<const uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return IoStatus::InvalidParameter;
  transferred = io.vtty->sink_write(buffer.data(), buffer.size());
  KE_TTY_PumpDevice(io.vtty);
  io.offset += transferred;
  return IoStatus::Success;
}

IoStatus TtySinkClose(KIOObject &io) {
  io.isClosed = true;
  if (io.vtty) io.vtty->close_sink();
  return IoStatus::Success;
}

IoStatus TtySourceRead(KIOObject &io, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return IoStatus::InvalidParameter;
  transferred = io.vtty->source_read(buffer.data(), buffer.size());
  io.offset += transferred;
  return IoStatus::Success;
}

IoStatus TtySourceWrite(KIOObject &io, kstd::span<const uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return IoStatus::InvalidParameter;
  transferred = io.vtty->source_write(buffer.data(), buffer.size());
  io.offset += transferred;
  return IoStatus::Success;
}

IoStatus TtySourceClose(KIOObject &io) {
  io.isClosed = true;
  if (io.vtty) io.vtty->close_source();
  return IoStatus::Success;
}

IoStatus DeviceRead(KIOObject &io, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (!io.device) return IoStatus::InvalidParameter;
  if (!(io.mode & KIOObject::OpenMode::Read)) return IoStatus::AccessDenied;
  const auto status = KE_IO_ReadDevice(io.device, io.offset, buffer, transferred);
  if (status == IoStatus::Success) io.offset += transferred;
  return status;
}

IoStatus DeviceWrite(KIOObject &io, kstd::span<const uint8_t> buffer, size_t &transferred) {
  if (!io.device) return IoStatus::InvalidParameter;
  if (!(io.mode & KIOObject::OpenMode::Write)) return IoStatus::AccessDenied;
  const auto status = KE_IO_WriteDevice(io.device, io.offset, buffer, transferred);
  if (status == IoStatus::Success) io.offset += transferred;
  return status;
}

IoStatus DeviceControl(KIOObject &io, uint32_t code, kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred) {
  if (!io.device) return IoStatus::InvalidParameter;
  return KE_IO_ControlDevice(io.device, code, input, output, transferred);
}

IoStatus DeviceClose(KIOObject &io) {
  io.isClosed = true;
  return IoStatus::Success;
}
}// namespace

namespace io {
const KIOOps TtySinkOps{TtySinkRead, TtySinkWrite, {}, TtySinkClose};
const KIOOps TtySourceOps{TtySourceRead, TtySourceWrite, {}, TtySourceClose};
const KIOOps DeviceOps{DeviceRead, DeviceWrite, DeviceControl, DeviceClose};
}// namespace io

}// namespace kernel


UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KIOObject> KE_IO_CreateTtySinkStream(
    const kernel::tty::VttyPtr &vtty,
    const kstd::string_view &name) noexcept {
  if (!vtty) return nullptr;
  auto ioObj = name.empty()
                   ? kernel::CreateAnonymousKObject<kernel::KIOObject>()
                   : kernel::CreateKObject<kernel::KIOObject>(name);
  if (!ioObj) return nullptr;

  ioObj->ops = &kernel::io::TtySinkOps;
  ioObj->vtty = vtty;
  ioObj->mode = kernel::KIOObject::OpenMode::ReadWrite;
  return ioObj;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KIOObject> KE_IO_CreateTtySourceStream(
    const kernel::tty::VttyPtr &vtty,
    const kstd::string_view &name) noexcept {
  if (!vtty) return nullptr;
  auto ioObj = name.empty()
                   ? kernel::CreateAnonymousKObject<kernel::KIOObject>()
                   : kernel::CreateKObject<kernel::KIOObject>(name);
  if (!ioObj) return nullptr;

  ioObj->ops = &kernel::io::TtySourceOps;
  ioObj->vtty = vtty;
  ioObj->mode = kernel::KIOObject::OpenMode::ReadWrite;
  return ioObj;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KIOObject> KE_IO_CreateDeviceStream(
    const kernel::KDevicePtr<kernel::KDevice> &device,
    kernel::KIOObject::OpenMode mode,
    const kstd::string_view &name) noexcept {
  if (!device) return nullptr;
  auto ioObj = name.empty()
                   ? kernel::CreateAnonymousKObject<kernel::KIOObject>()
                   : kernel::CreateKObject<kernel::KIOObject>(name);
  if (!ioObj) return nullptr;

  ioObj->ops = &kernel::io::DeviceOps;
  ioObj->device = device;
  ioObj->mode = mode;
  return ioObj;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KIOObject> KE_IO_FromObject(
    const kernel::KObjectPtr<kernel::KObject> &obj) noexcept {
  if (!obj) return nullptr;

  if (auto ioObj = obj.As<kernel::KIOObject>()) {
    return ioObj;
  }


  if (auto sink = obj.As<kernel::KVttySinkObject>()) {
    return KE_IO_CreateTtySinkStream(sink->vtty, sink->name);
  }

  if (auto source = obj.As<kernel::KVttySourceObject>()) {
    return KE_IO_CreateTtySourceStream(source->vtty, source->name);
  }

  if (auto devObj = obj.As<kernel::KDeviceObject>()) {
    if (devObj->device) {
      return KE_IO_CreateDeviceStream(devObj->device, kernel::KIOObject::OpenMode::ReadWrite, devObj->name);
    }
  }

  return nullptr;
}
