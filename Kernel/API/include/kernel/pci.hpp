
#pragma once

#include <kernel/__core.hpp>
#include <kernel/kerror.hpp>
#include <kernel/device.hpp>
#include <kernel/fwd/KDevicePtr.hpp>
#include <kernel/kobject/KDriverObject.hpp>
#include <span.hpp>

// Interface exposed by the bus_pci driver to drivers of its child PDOs.
//
// A child driver sends a DeviceControl request to its own PDO (the PDO is owned
// by bus_pci, so the request lands in the PCI bus driver). Use the inline
// helpers below rather than issuing the codes by hand.
//
// Interrupts: PCI INTx is level-triggered and may be shared, while the x86_old
// interrupt path is an edge-triggered 8259 PIC. A child ISR MUST clear the
// interrupt cause in its device before returning, or the line stays asserted
// and no further edge is seen.
namespace kernel::pci {

enum class IrqSource : uint8_t {
  None = 0,     // no INTx (pin 0) or no usable line
  Pir,          // Interrupt Line confirmed by the $PIR routing table
  InterruptLine,// BIOS-assigned Interrupt Line register, unconfirmed
};

struct Bar {
  uint32_t base = 0;// decoded base (I/O port or physical address); 0 = unused
  uint32_t size = 0;// 0 = unimplemented
  bool io = false;
  bool prefetch = false;
  bool is64 = false;// 64-bit memory BAR (consumes the next BAR slot)
};

struct DeviceInfo {
  uint8_t bus = 0, dev = 0, fn = 0;
  uint16_t vendorId = 0, deviceId = 0;
  uint16_t subVendorId = 0, subDeviceId = 0;
  uint8_t classCode = 0, subClass = 0, progIf = 0, revision = 0, headerType = 0;
  Bar bars[6]{};
  uint8_t barCount = 0;// 6 for header type 0, 2 for type 1
  uint8_t intPin = 0;  // 0 = none, 1..4 = INTA..INTD
  int16_t irq = -1;    // PIC IRQ 0..15, or -1
  IrqSource irqSource = IrqSource::None;
  uint8_t msiCapOffset = 0;// 0 = no MSI capability
  bool msi64 = false;
  bool msiPerVectorMask = false;
};

// DeviceControl codes handled by bus_pci on its child PDOs.
constexpr uint32_t IOCTL_PCI_GET_DEVICE_INFO = 0x50430001;// out: DeviceInfo
constexpr uint32_t IOCTL_PCI_CONFIG_READ = 0x50430002;    // in: ConfigRequest, out: uint32_t
constexpr uint32_t IOCTL_PCI_CONFIG_WRITE = 0x50430003;   // in: ConfigRequest
constexpr uint32_t IOCTL_PCI_ENABLE_MSI = 0x50430004;     // in: MsiRequest

struct ConfigRequest {
  uint8_t offset;
  uint8_t width;// 1, 2 or 4; offset must be naturally aligned
  uint32_t value;
};

struct MsiRequest {
  uint8_t vector;
  uint32_t lapicId;
};

namespace detail {
inline kstd::error_code Call(const KDevicePtr<KDevice> &pdo, uint32_t code, kstd::span<const uint8_t> in, kstd::span<uint8_t> out) {
  if (!pdo || !pdo->driverObject || !pdo->driverObject->DeviceControl) return KernelError::Unsupported;
  size_t transferred = 0;
  return pdo->driverObject->DeviceControl(pdo, code, in, out, transferred);
}
}// namespace detail

inline kstd::error_code GetDeviceInfo(const KDevicePtr<KDevice> &pdo, DeviceInfo &info) {
  return detail::Call(pdo, IOCTL_PCI_GET_DEVICE_INFO, {}, {reinterpret_cast<uint8_t *>(&info), sizeof(info)});
}

inline kstd::error_code ConfigRead(const KDevicePtr<KDevice> &pdo, uint8_t offset, uint8_t width, uint32_t &value) {
  const ConfigRequest req{.offset = offset, .width = width, .value = 0};
  return detail::Call(pdo, IOCTL_PCI_CONFIG_READ, {reinterpret_cast<const uint8_t *>(&req), sizeof(req)}, {reinterpret_cast<uint8_t *>(&value), sizeof(value)});
}

inline kstd::error_code ConfigWrite(const KDevicePtr<KDevice> &pdo, uint8_t offset, uint8_t width, uint32_t value) {
  const ConfigRequest req{.offset = offset, .width = width, .value = value};
  return detail::Call(pdo, IOCTL_PCI_CONFIG_WRITE, {reinterpret_cast<const uint8_t *>(&req), sizeof(req)}, {});
}

// Returns Unsupported while the HAL has no local APIC (always, on x86_old);
// the device then keeps using INTx.
inline kstd::error_code EnableMsi(const KDevicePtr<KDevice> &pdo, uint8_t vector, uint32_t lapicId) {
  const MsiRequest req{.vector = vector, .lapicId = lapicId};
  return detail::Call(pdo, IOCTL_PCI_ENABLE_MSI, {reinterpret_cast<const uint8_t *>(&req), sizeof(req)}, {});
}

}// namespace kernel::pci
