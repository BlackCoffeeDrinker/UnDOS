

#include "isa_pnp.hpp"

#include <Kernel.hpp>

namespace kernel::isa {

static void WriteConfigReg(PnpRegister reg) {
  HAL_IO_Out8(PNP_ADDRESS_PORT, static_cast<uint8_t>(reg));
}

static void WriteData(uint8_t val) {
  HAL_IO_Out8(PNP_WRITE_DATA_PORT, val);
}

static uint8_t ReadData() {
  return HAL_IO_In8(PNP_READ_DATA_PORT);
}

static void SendInitiationKey() {
  uint8_t val = 0x6A;
  HAL_IO_Out8(PNP_ADDRESS_PORT, 0);
  HAL_IO_Out8(PNP_ADDRESS_PORT, 0);
  for (int i = 0; i < 32; i++) {
    HAL_IO_Out8(PNP_ADDRESS_PORT, val);
    const uint8_t bit = ((val >> 0) ^ (val >> 1)) & 1;
    val = (val >> 1) | (bit << 7);
  }
}

static bool Isolate(uint8_t (&serial)[9]) {
  uint8_t checksum = 0x6A;
  WriteConfigReg(PnpRegister::SerialIsolation);
  HAL_IO_Delay();

  for (int i = 0; i < 64; i++) {
    const uint8_t b1 = ReadData();
    const uint8_t b2 = ReadData();
    const uint8_t bit = (b1 == 0x55 && b2 == 0xAA);
    serial[i / 8] |= (bit << (i % 8));
    const uint8_t lsb = (checksum ^ (checksum >> 1) ^ bit) & 1;
    checksum = (checksum >> 1) | (lsb << 7);
  }

  for (int i = 0; i < 8; i++) {
    const uint8_t b1 = ReadData();
    const uint8_t b2 = ReadData();
    serial[8] |= ((b1 == 0x55 && b2 == 0xAA) << i);
  }

  return serial[8] == checksum && (serial[0] != 0 || serial[1] != 0);
}

static void EnumerateDevices(const KObjectPtr<KDriverObject>& driver) {
  early_print("ISA PnP: Starting enumeration...\r\n");
  SendInitiationKey();

  WriteConfigReg(PnpRegister::ConfigControl);
  WriteData(CONTROL_RESET_CSN);
  HAL_IO_Delay();
  HAL_IO_Delay();

  WriteConfigReg(PnpRegister::SetReadDataPort);
  WriteData(PNP_READ_DATA_PORT >> 2);
  HAL_IO_Delay();

  uint8_t csn = 1;
  while (csn < 32) {
    WriteConfigReg(PnpRegister::Wake);
    WriteData(0);
    HAL_IO_Delay();

    uint8_t serial[9] = {0};
    if (Isolate(serial)) {
      early_print_fmt("ISA PnP: Found card, ID: {x:02}{x:02}{x:02}{x:02} Serial: {x:02}{x:02}{x:02}{x:02}\r\n",
                      serial[0], serial[1], serial[2], serial[3],
                      serial[4], serial[5], serial[6], serial[7]);

      WriteConfigReg(PnpRegister::CardSelectNumber);
      WriteData(csn);

      if (const auto pdo = KE_CreateObject<KDeviceObject>(driver)) {
        pdo->name = "ISAPNP_PDO";
        KE_PNP_ReportNewDevice(nullptr, pdo);
      }
      csn++;
    } else {
      if (csn == 1) early_print("ISA PnP: No cards found.\r\n");
      break;
    }
  }

  WriteConfigReg(PnpRegister::ConfigControl);
  WriteData(CONTROL_WAIT_FOR_KEY);
}

static void HandleEvent(const KObjectPtr<KDriverObject>& driver, const KEvent &event) {
  if (event.type == EventType::Pnp) {
    if (auto &pnpEvent = static_cast<const KPnpEvent &>(event);
        pnpEvent.minorFunction == PnpMinorFunction::QueryDeviceRelations) {
      EnumerateDevices(driver);
    }
  }
}

}// namespace kernel::isa

extern "C" void DriverEntry(kernel::KObjectPtr<kernel::KDriverObject> &driver) {
  early_print("DriverEntry called!\r\n");
  driver->name = "ISAPNP";
  driver->eventHandler = kernel::isa::HandleEvent;

  if (const auto busDevice = KE_CreateObject<kernel::KDeviceObject>(driver)) {
    busDevice->name = "IsaPnpBus";
    busDevice->deviceType = kernel::DeviceType::Bus;
    KE_PNP_EnumerateBus(busDevice);
  }
}
