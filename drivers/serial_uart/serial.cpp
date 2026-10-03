#include <Kernel.hpp>
#include <kernel/device.hpp>
#include <kernel/driver.hpp>
#include <kernel/resource.hpp>
#include <kernel/tty/tty_device.hpp>
#include <kernel/hal_interface.hpp>

namespace {

constexpr uint16_t UART_DATA = 0;
constexpr uint16_t UART_IER  = 1;
constexpr uint16_t UART_FCR  = 2;
constexpr uint16_t UART_LCR  = 3;
constexpr uint16_t UART_MCR  = 4;
constexpr uint16_t UART_LSR  = 5;

struct SerialDeviceExtension {
  uint16_t ioBase{0x3F8};
  uint32_t irq{4};
};

void InitUart(uint16_t base) {
  HAL_IO_Out8(base + UART_IER, 0x00); // Disable interrupts (polled I/O)
  HAL_IO_Out8(base + UART_LCR, 0x80); // Enable DLAB
  HAL_IO_Out8(base + UART_DATA, 0x03); // Divisor 3 (38400 baud)
  HAL_IO_Out8(base + UART_IER, 0x00);
  HAL_IO_Out8(base + UART_LCR, 0x03); // 8 bits, no parity, 1 stop bit
  HAL_IO_Out8(base + UART_FCR, 0xC7); // Enable FIFO, clear RX/TX FIFOs, 14-byte threshold
  HAL_IO_Out8(base + UART_MCR, 0x03); // RTS, DTR
}

bool Serial_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = KE_IO_CreateDeviceWithContext<SerialDeviceExtension>(
      driver, "Serial", kernel::device_type::Serial, CreateDeviceFlags::MultipleAllowed);
  if (!fdo) {
    KE_Print("serial_uart: failed to create Serial device");
    return false;
  }

  KE_IO_AttachFilterDevice(fdo, pdo);
  return true;
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

  InitUart(ext->ioBase);

  KE_Print("serial_uart: Serial device started at I/O 0x{:x}, IRQ {} -> linked to VTTY",
                  ext->ioBase, ext->irq);
}

kernel::IoStatus Serial_Read(const kernel::KDevicePtr<kernel::KDevice> &device,
                             uint64_t /*offset*/,
                             kstd::span<uint8_t> buffer,
                             size_t &transferred) {
  transferred = 0;
  if (!device || buffer.empty()) return kernel::IoStatus::Success;

  const auto *ext = device->deviceExtension.as<SerialDeviceExtension>();
  if (!ext) return kernel::IoStatus::DeviceError;

  const uint16_t base = ext->ioBase;
  while (transferred < buffer.size()) {
    if ((HAL_IO_In8(base + UART_LSR) & 0x01) == 0) {
      break;
    }
    buffer[transferred++] = HAL_IO_In8(base + UART_DATA);
  }

  return kernel::IoStatus::Success;
}

kernel::IoStatus Serial_Write(const kernel::KDevicePtr<kernel::KDevice> &device,
                              uint64_t /*offset*/,
                              kstd::span<const uint8_t> buffer,
                              size_t &transferred) {
  transferred = 0;
  if (!device || buffer.empty()) return kernel::IoStatus::Success;

  const auto *ext = device->deviceExtension.as<SerialDeviceExtension>();
  if (!ext) return kernel::IoStatus::DeviceError;

  const uint16_t base = ext->ioBase;
  while (transferred < buffer.size()) {
    if ((HAL_IO_In8(base + UART_LSR) & 0x20) == 0) {
      break;
    }
    HAL_IO_Out8(base + UART_DATA, buffer[transferred++]);
  }

  return kernel::IoStatus::Success;
}

} // namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = Serial_AddDevice;
  driver->StartDevice = Serial_StartDevice;
  driver->Read = Serial_Read;
  driver->Write = Serial_Write;
}
