#pragma once

#include <kernel/__core.hpp>

namespace kernel::isa {

// I/O Ports
constexpr uint16_t PNP_ADDRESS_PORT = 0x0279;
constexpr uint16_t PNP_WRITE_DATA_PORT = 0x0A79;
constexpr uint16_t PNP_READ_DATA_PORT = 0x0203;

// Config Control Bits
constexpr uint8_t CONTROL_RESET_CSN = 0x04; // Bit 2
constexpr uint8_t CONTROL_WAIT_FOR_KEY = 0x02; // Bit 1
constexpr uint8_t CONTROL_RESET_DRV = 0x01; // Bit 0

// Config Registers
enum class PnpRegister : uint8_t {
  SetReadDataPort = 0x00,
  SerialIsolation = 0x01,
  ConfigControl = 0x02,
  Wake = 0x03,
  ResourceData = 0x04,
  Status = 0x05,
  CardSelectNumber = 0x06,
  LogicalDeviceNumber = 0x07,
  
  // Logical Device Configuration
  Activate = 0x30,
  IoRangeCheck = 0x31,
  
  // I/O Descriptors (up to 8)
  IoBaseHigh0 = 0x60,
  IoBaseLow0 = 0x61,
  IoBaseHigh1 = 0x62,
  IoBaseLow1 = 0x63,
  
  // IRQ Descriptors (up to 2)
  IrqLevel0 = 0x70,
  IrqType0 = 0x71,
  IrqLevel1 = 0x72,
  IrqType1 = 0x73,
  
  // DMA Descriptors (up to 2)
  DmaChannel0 = 0x74,
  DmaChannel1 = 0x75,
};

} // namespace kernel::isa
