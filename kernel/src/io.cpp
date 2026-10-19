
#include "stdkrn.hpp"
#include <kernel/dispatch.hpp>
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/tty/tty.hpp>
#include <kernel/tty/tty_device.hpp>
#include <kernel/vfs.hpp>

namespace {
kernel::KernelError setupCommonKDevice(const kernel::KDevicePtr<kernel::KDevice> &device,
                        const kernel::KObjectPtr<kernel::KDriverObject> &driver,
                        size_t deviceExtensionSize,
                        const kstd::string_view &deviceName, CreateDeviceFlags flags) {

  device->driverObject = driver;

  if (deviceExtensionSize > 0) {
    device->deviceExtension = kernel::DataBuffer::Alloc(deviceExtensionSize);
    if (!device->deviceExtension) {
      return kernel::KernelError::OutOfMemory;
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
      if (!num) return num.error();
      kstd::format(deviceNameStr, "{}{}", deviceName, *num);
    } else {
      kstd::format(deviceNameStr, "{}", deviceName);
    }

    auto deviceObj = kernel::CreateKObject<kernel::KDeviceObject>(deviceNameStr);
    if (!deviceObj) return deviceObj.error();
    (*deviceObj)->device = device;
    device->deviceObject = *deviceObj;

    if (const auto status = KE_OB_InsertObject(KE_OB_GetDeviceDirectory(), *deviceObj); status != kernel::KernelError::Success) {
      KE_Print("KE_OB_InsertObject failed for device {}", deviceName);
      // Collision ?
      return status;
    }
  }

  return kernel::KernelError::Success;
}
}// namespace

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KDevicePtr<kernel::KDevice>, kernel::KernelError> KE_IO_CreateDevice(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    size_t deviceExtensionSize,
    const kstd::string_view &baseDeviceName,
    kernel::DeviceType deviceType, CreateDeviceFlags flags) noexcept {

  auto device = KE_CreateKPtrObject<kernel::KDevice>(driver);
  if (!device) return kstd::unexpected(device.error());
  if (const auto status = setupCommonKDevice(*device, driver, deviceExtensionSize, baseDeviceName, flags); status != kernel::KernelError::Success)
    return kstd::unexpected(status);
  (*device)->deviceType = deviceType;
  return kstd::move(*device);
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KDevicePtr<kernel::KDeviceBus>, kernel::KernelError> KE_IO_CreateBus(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    size_t deviceExtensionSize,
    const kstd::string_view &busName, CreateDeviceFlags flags) noexcept {

  auto device = KE_CreateKPtrObject<kernel::KDeviceBus>(driver);
  if (!device) return kstd::unexpected(device.error());
  if (const auto status = setupCommonKDevice(*device, driver, deviceExtensionSize, busName, flags); status != kernel::KernelError::Success)
    return kstd::unexpected(status);
  (*device)->deviceType = kernel::device_type::Bus;
  return kstd::move(*device);
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KDevicePtr<kernel::KDevice>, kernel::KernelError> KE_IO_AttachFilterDevice(
    const kernel::KDevicePtr<kernel::KDevice> &sourceDevice,
    const kernel::KDevicePtr<kernel::KDevice> &targetDevice) noexcept {
  if (!sourceDevice || !targetDevice)
    return kstd::unexpected(kernel::KernelError::InvalidArgument);

  // Walk to the current top of the target's device stack.
  auto top = targetDevice;
  while (top->attachedDevice) {
    top = top->attachedDevice;
  }

  top->attachedDevice = sourceDevice;
  sourceDevice->lowerDevice = top;

  return top;
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_IO_AttachChildDevice(const kernel::KDevicePtr<kernel::KDeviceBus> &bus,
                                                                                 kernel::KDevicePtr<kernel::KDevice> child) noexcept {

  if (!bus || !child) return kernel::KernelError::InvalidArgument;

  // Sanity checks
  if (child->parentBus != nullptr) [[unlikely]] {
    KE_Print("KE_IO_AttachChildDevice: Device {} already has a parent bus ({}).", child->Name(), child->parentBus->Name());
    return kernel::KernelError::AlreadyAttached;
  }


  bus->children.push_back(child);
  child->parentBus = bus;
  KE_Print("KE_IO_AttachChildDevice: Bus = {} Child = {}", bus->Name(), child->Name());

  return kernel::KernelError::Success;
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_IO_ReadDevice(
    const kernel::KDevicePtr<kernel::KDevice> &device,
    uint64_t offset,
    kstd::span<uint8_t> buffer,
    size_t &transferred) noexcept {

  transferred = 0;

  if (!device || !buffer.data()) return kernel::KernelError::InvalidArgument;
  if (buffer.empty()) return kernel::KernelError::Success;

  if (!device->driverObject || !device->driverObject->Read) {
    return kernel::KernelError::Unsupported;
  }
  return device->driverObject->Read(device, offset, buffer, transferred);
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_IO_WriteDevice(
    const kernel::KDevicePtr<kernel::KDevice> &device,
    uint64_t offset,
    kstd::span<const uint8_t> buffer,
    size_t &transferred) noexcept {
  transferred = 0;
  if (!device || !buffer.data()) return kernel::KernelError::InvalidArgument;
  if (buffer.empty()) return kernel::KernelError::Success;
  if (!device->driverObject || !device->driverObject->Write) {
    return kernel::KernelError::Unsupported;
  }
  return device->driverObject->Write(device, offset, buffer, transferred);
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_IO_ControlDevice(
    const kernel::KDevicePtr<kernel::KDevice> &device,
    uint32_t code,
    kstd::span<const uint8_t> input,
    kstd::span<uint8_t> output,
    size_t &transferred) noexcept {
  transferred = 0;
  if (!device) return kernel::KernelError::InvalidArgument;
  if (!device->driverObject || !device->driverObject->DeviceControl) {
    return kernel::KernelError::Unsupported;
  }
  return device->driverObject->DeviceControl(device, code, input, output, transferred);
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject>, kernel::KernelError> KE_IO_ConnectInterrupt(
    const kernel::KDevicePtr<kernel::KDevice> & /*device*/,
    uint32_t irq,
    kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)> routine,
    void *context) noexcept {
  if (!routine) return kstd::unexpected(kernel::KernelError::InvalidArgument);

  auto created = kernel::CreateAnonymousKObject<kernel::KInterruptServiceRoutineObject>();
  if (!created) return kstd::unexpected(created.error());
  auto isr = kstd::move(*created);

  isr->service_routine = routine;
  isr->service_context = context;
  isr->irql = 0;

  if (const auto status = KE_DISPATCH_RegisterHandler(irq, isr); status != kernel::KernelError::Success) {
    return kstd::unexpected(status);
  }

  if (!HAL_INTR_ConnectInterrupt(irq, isr)) {
    KE_DISPATCH_UnregisterHandler(irq);
    return kstd::unexpected(kernel::KernelError::ResourceConflict);
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

KernelError KIOObject::Read(kstd::span<uint8_t> buffer, size_t &transferred) {
  transferred = 0;
  if (isClosed) return KernelError::DeviceError;
  if (!ops || !ops->Read) return KernelError::Unsupported;
  return ops->Read(*this, buffer, transferred);
}

KernelError KIOObject::Write(kstd::span<const uint8_t> buffer, size_t &transferred) {
  transferred = 0;
  if (isClosed) return KernelError::DeviceError;
  if (!ops || !ops->Write) return KernelError::Unsupported;
  return ops->Write(*this, buffer, transferred);
}

KernelError KIOObject::Control(uint32_t code, kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred) {
  transferred = 0;
  if (isClosed) return KernelError::DeviceError;
  if (!ops || !ops->Control) return KernelError::Unsupported;
  return ops->Control(*this, code, input, output, transferred);
}

KernelError KIOObject::Close() {
  if (isClosed) return KernelError::Success;
  if (!ops || !ops->Close) {
    isClosed = true;
    return KernelError::Success;
  }
  return ops->Close(*this);
}

namespace {
KernelError TtySinkRead(KIOObject &io, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return KernelError::InvalidArgument;
  KE_TTY_PumpDevice(io.vtty);
  if (io.blockingRead) {
    if (const auto status = io.vtty->sink_read_wait(buffer.data(), buffer.size(), transferred); status != KernelError::Success) {
      transferred = 0;
      return status;
    }
  } else {
    transferred = io.vtty->sink_read(buffer.data(), buffer.size());
  }
  io.offset += transferred;
  return KernelError::Success;
}

KernelError TtySinkWrite(KIOObject &io, kstd::span<const uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return KernelError::InvalidArgument;
  const auto written = io.vtty->sink_write(buffer.data(), buffer.size());
  if (!written) return written.error();
  transferred = *written;
  KE_TTY_PumpDevice(io.vtty);
  io.offset += transferred;
  return KernelError::Success;
}

KernelError TtySinkClose(KIOObject &io) {
  io.isClosed = true;
  if (io.vtty && io.ownsTtyEndpoint) io.vtty->close_sink();
  return KernelError::Success;
}

KernelError TtySourceRead(KIOObject &io, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return KernelError::InvalidArgument;
  transferred = io.vtty->source_read(buffer.data(), buffer.size());
  io.offset += transferred;
  return KernelError::Success;
}

KernelError TtySourceWrite(KIOObject &io, kstd::span<const uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return KernelError::InvalidArgument;
  const auto written = io.vtty->source_write(buffer.data(), buffer.size());
  if (!written) return written.error();
  transferred = *written;
  io.offset += transferred;
  return KernelError::Success;
}

KernelError TtySourceClose(KIOObject &io) {
  io.isClosed = true;
  if (io.vtty) io.vtty->close_source();
  return KernelError::Success;
}

KernelError DeviceRead(KIOObject &io, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (!io.device) return KernelError::InvalidArgument;
  if (!(io.mode & KIOObject::OpenMode::Read)) return KernelError::AccessDenied;
  const auto status = KE_IO_ReadDevice(io.device, io.offset, buffer, transferred);
  if (status == KernelError::Success) io.offset += transferred;
  return status;
}

KernelError DeviceWrite(KIOObject &io, kstd::span<const uint8_t> buffer, size_t &transferred) {
  if (!io.device) return KernelError::InvalidArgument;
  if (!(io.mode & KIOObject::OpenMode::Write)) return KernelError::AccessDenied;
  const auto status = KE_IO_WriteDevice(io.device, io.offset, buffer, transferred);
  if (status == KernelError::Success) io.offset += transferred;
  return status;
}

KernelError DeviceControl(KIOObject &io, uint32_t code, kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred) {
  if (!io.device) return KernelError::InvalidArgument;
  return KE_IO_ControlDevice(io.device, code, input, output, transferred);
}

KernelError DeviceClose(KIOObject &io) {
  io.isClosed = true;
  return KernelError::Success;
}
}// namespace

namespace io {
const KIOOps TtySinkOps{.Read = TtySinkRead, .Write = TtySinkWrite, .Control = {}, .Close = TtySinkClose};
const KIOOps TtySourceOps{.Read = TtySourceRead, .Write = TtySourceWrite, .Control = {}, .Close = TtySourceClose};
const KIOOps DeviceOps{.Read = DeviceRead, .Write = DeviceWrite, .Control = DeviceControl, .Close = DeviceClose};
}// namespace io

}// namespace kernel


UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KIOObject>, kernel::KernelError> KE_IO_CreateTtySinkStream(
    const kernel::tty::VttyPtr &vtty,
    const kstd::string_view &name) noexcept {
  if (!vtty) return kstd::unexpected(kernel::KernelError::InvalidArgument);
  auto created = name.empty()
                     ? kernel::CreateAnonymousKObject<kernel::KIOObject>()
                     : kernel::CreateKObject<kernel::KIOObject>(name);
  if (!created) return kstd::unexpected(created.error());
  auto ioObj = kstd::move(*created);

  ioObj->ops = &kernel::io::TtySinkOps;
  ioObj->vtty = vtty;
  ioObj->mode = kernel::KIOObject::OpenMode::ReadWrite;
  return ioObj;
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KIOObject>, kernel::KernelError> KE_IO_CreateTtySourceStream(
    const kernel::tty::VttyPtr &vtty,
    const kstd::string_view &name) noexcept {
  if (!vtty) return kstd::unexpected(kernel::KernelError::InvalidArgument);
  auto created = name.empty()
                     ? kernel::CreateAnonymousKObject<kernel::KIOObject>()
                     : kernel::CreateKObject<kernel::KIOObject>(name);
  if (!created) return kstd::unexpected(created.error());
  auto ioObj = kstd::move(*created);

  ioObj->ops = &kernel::io::TtySourceOps;
  ioObj->vtty = vtty;
  ioObj->mode = kernel::KIOObject::OpenMode::ReadWrite;
  return ioObj;
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KIOObject>, kernel::KernelError> KE_IO_CreateDeviceStream(
    const kernel::KDevicePtr<kernel::KDevice> &device,
    kernel::KIOObject::OpenMode mode,
    const kstd::string_view &name) noexcept {
  if (!device) return kstd::unexpected(kernel::KernelError::InvalidArgument);
  auto created = name.empty()
                     ? kernel::CreateAnonymousKObject<kernel::KIOObject>()
                     : kernel::CreateKObject<kernel::KIOObject>(name);
  if (!created) return kstd::unexpected(created.error());
  auto ioObj = kstd::move(*created);

  ioObj->ops = &kernel::io::DeviceOps;
  ioObj->device = device;
  ioObj->mode = mode;
  return ioObj;
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KIOObject>, kernel::KernelError> KE_IO_FromObject(
    const kernel::KObjectPtr<kernel::KObject> &obj) noexcept {
  if (!obj) return kstd::unexpected(kernel::KernelError::InvalidHandle);

  if (auto ioObj = obj.As<kernel::KIOObject>()) {
    return ioObj;
  }


  if (const auto sink = obj.As<kernel::KVttySinkObject>()) {
    return KE_IO_CreateTtySinkStream(sink->vtty, sink->name);
  }

  if (const auto source = obj.As<kernel::KVttySourceObject>()) {
    return KE_IO_CreateTtySourceStream(source->vtty, source->name);
  }

  if (const auto devObj = obj.As<kernel::KDeviceObject>()) {
    if (devObj->device) {
      return KE_IO_CreateDeviceStream(devObj->device, kernel::KIOObject::OpenMode::ReadWrite, devObj->name);
    }
  }

  return kstd::unexpected(kernel::KernelError::WrongObjectType);
}
