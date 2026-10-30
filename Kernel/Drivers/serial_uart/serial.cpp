#include <Kernel.hpp>
#include <kernel/device.hpp>
#include <kernel/driver.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/resource.hpp>
#include <kernel/tty/tty_device.hpp>
#include "uart_io.hpp"

namespace {
uint8_t ReadPort(void *, uint16_t port) noexcept {
  return HAL_IO_In8(port);
}

void WritePort(void *, uint16_t port, uint8_t value) noexcept {
  HAL_IO_Out8(port, value);
}

uintptr_t SaveAndDisable(void *) noexcept {
  return HAL_CPU_SaveAndDisableInterrupts();
}

void RestoreInterrupts(void *, uintptr_t state) noexcept {
  HAL_CPU_RestoreInterrupts(state);
}

serial_uart::detail::Uart uart(
    {nullptr, ReadPort, WritePort},
    {nullptr, SaveAndDisable, RestoreInterrupts});

struct SerialDeviceExtension {
  uint16_t ioBase{0x3F8};
  uint32_t irq{4};
};

kstd::error_code Serial_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = KE_IO_CreateDeviceWithContext<SerialDeviceExtension>(
      driver, "Serial", kernel::device_type::Serial, CreateDeviceFlags::MultipleAllowed);
  if (!fdo) {
    KE_Print("serial_uart: failed to create Serial device");
    return fdo.error();
  }

  if (const auto attached = KE_IO_AttachFilterDevice(*fdo, pdo); !attached)
    return attached.error();
  return {};
}

void Serial_StartDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = pdo->attachedDevice;
  if (!fdo) {
    KE_Print("serial_uart: no attached FDO for PDO");
    return;
  }

  auto *ext = fdo->deviceExtension.as<SerialDeviceExtension>();
  if (!ext) return;

  const auto *portRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Port, 0);
  ext->ioBase = portRes ? static_cast<uint16_t>(portRes->start) : static_cast<uint16_t>(0x3F8);

  const auto *irqRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Interrupt, 0);
  ext->irq = irqRes ? irqRes->start : 4;

  if (!uart.Start(ext->ioBase)) {
    KE_Print("serial_uart: UART at I/O 0x{:x} is unavailable", ext->ioBase);
    return;
  }

  KE_Print("serial_uart: Serial device started at I/O 0x{:x}, IRQ {} -> linked to VTTY",
           ext->ioBase, ext->irq);
}

kstd::error_code Serial_Read(const kernel::KDevicePtr<kernel::KDevice> &device,
                                uint64_t /*offset*/,
                                kstd::span<uint8_t> buffer,
                                size_t &transferred) {
  transferred = 0;
  if (!device || buffer.empty()) return kernel::KernelError::Success;

  const auto *ext = device->deviceExtension.as<SerialDeviceExtension>();
  if (!ext) return kernel::KernelError::DeviceError;

  return uart.Read(ext->ioBase, buffer.data(), buffer.size(), transferred);
}

kstd::error_code Serial_Write(const kernel::KDevicePtr<kernel::KDevice> &device,
                                 uint64_t /*offset*/,
                                 kstd::span<const uint8_t> buffer,
                                 size_t &transferred) {
  transferred = 0;
  if (!device || buffer.empty()) return kernel::KernelError::Success;

  const auto *ext = device->deviceExtension.as<SerialDeviceExtension>();
  if (!ext) return kernel::KernelError::DeviceError;

  return uart.Write(ext->ioBase, buffer.data(), buffer.size(), transferred);
}

}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = Serial_AddDevice;
  driver->StartDevice = Serial_StartDevice;
  driver->Read = Serial_Read;
  driver->Write = Serial_Write;
}
