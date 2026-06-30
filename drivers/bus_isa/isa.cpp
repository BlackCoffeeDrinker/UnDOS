

#include "isa_pnp.hpp"
#include <kernel/hal_interface.hpp>
#include <kernel/io.hpp>
#include <kernel/event.hpp>
#include <kernel/pnp.hpp>
#include <kernel/memory/virtual_memory.hpp>

namespace kernel::isa {

static uint16_t s_readDataPort = 0x0203;

static void WriteConfigReg(PnpRegister reg) {
  HAL_IO_Out8(PNP_ADDRESS_PORT, static_cast<uint8_t>(reg));
}

static void WriteData(uint8_t val) {
  HAL_IO_Out8(PNP_WRITE_DATA_PORT, val);
}

static uint8_t ReadData() {
  return HAL_IO_In8(s_readDataPort);
}

static void SendInitiationKey() {
  uint8_t val = 0x6A;
  HAL_IO_Out8(PNP_ADDRESS_PORT, 0);
  HAL_IO_Out8(PNP_ADDRESS_PORT, 0);
  for (int i = 0; i < 32; i++) {
    HAL_IO_Out8(PNP_ADDRESS_PORT, val);
    uint8_t bit = ((val >> 0) ^ (val >> 1)) & 1;
    val = (val >> 1) | (bit << 7);
  }
}

class IsaPnpBusDriver : public KDriverObject {
public:
  IsaPnpBusDriver() noexcept : KDriverObject() {
    name = "ISAPNP";
    eventHandler = [](KObjectPtr<KDriverObject> driver, const KEvent &event) {
      if (auto self = static_cast<IsaPnpBusDriver *>(driver.get())) {
        self->HandleEvent(event);
      }
    };
  }

  void HandleEvent(const KEvent &event) {
    if (event.type == EventType::Pnp) {
      auto &pnpEvent = static_cast<const KPnpEvent &>(event);
      if (pnpEvent.minorFunction == PnpMinorFunction::QueryDeviceRelations) {
        EnumerateDevices();
      }
    }
  }

  void EnumerateDevices() {
    early_print("ISA PnP: Starting enumeration...\n");
    SendInitiationKey();

    // Reset all CSNs
    WriteConfigReg(PnpRegister::ConfigControl);
    WriteData(0x04); // CONTROL_RESET_CSN
    HAL_IO_Delay();
    HAL_IO_Delay();

    // Set Read Data Port
    WriteConfigReg(PnpRegister::SetReadDataPort);
    WriteData(static_cast<uint8_t>(s_readDataPort >> 2));
    HAL_IO_Delay();

    uint8_t csn = 1;
    while (csn < 32) {
      // Wake all cards with CSN 0
      WriteConfigReg(PnpRegister::Wake);
      WriteData(0);
      HAL_IO_Delay();

      uint8_t serial[9] = {0};
      if (Isolate(serial)) {
        early_print_fmt("ISA PnP: Found card, ID: {x:02}{x:02}{x:02}{x:02} Serial: {x:02}{x:02}{x:02}{x:02}\n",
                        serial[0], serial[1], serial[2], serial[3],
                        serial[4], serial[5], serial[6], serial[7]);

        // Assign CSN
        WriteConfigReg(PnpRegister::CardSelectNumber);
        WriteData(csn);
        
        // Report device
        auto pdo = KE_CreateObject<KDeviceObject>(KObjectPtr<KDriverObject>(this));
        if (pdo) {
            pdo->name = "ISAPNP_PDO";
            Ke_PNP_ReportNewDevice(nullptr, pdo);
        }

        csn++;
      } else {
        if (csn == 1) {
          early_print("ISA PnP: No cards found.\n");
        }
        break;
      }
    }
    
    // Go to Wait for Key state
    WriteConfigReg(PnpRegister::ConfigControl);
    WriteData(0x02); // CONTROL_WAIT_FOR_KEY
  }

private:
  bool Isolate(uint8_t *serial) {
    uint8_t checksum = 0x6A;
    // Serial isolation register
    WriteConfigReg(PnpRegister::SerialIsolation);
    HAL_IO_Delay();

    for (int i = 0; i < 64; i++) {
      uint8_t b1 = ReadData();
      uint8_t b2 = ReadData();

      uint8_t bit = (b1 == 0x55 && b2 == 0xAA);
      serial[i / 8] |= (bit << (i % 8));

      uint8_t lsb = (checksum ^ (checksum >> 1) ^ bit) & 1;
      checksum = (checksum >> 1) | (lsb << 7);
    }

    for (int i = 0; i < 8; i++) {
      uint8_t b1 = ReadData();
      uint8_t b2 = ReadData();
      serial[8] |= ((b1 == 0x55 && b2 == 0xAA) << i);
    }

    return serial[8] == checksum && (serial[0] != 0 || serial[1] != 0);
  }
};

} // namespace kernel::isa

extern "C" void IsaPnpInit() {
    auto driver = kernel::KE_CreateObject<kernel::isa::IsaPnpBusDriver>();
    if (driver) {
        Ke_PNP_RegisterDriver(kernel::KObjectPtr<kernel::KDriverObject>(driver));
        
        // Create a root bus device for ISA PnP
        auto busDevice = kernel::KE_CreateObject<kernel::KDeviceObject>(kernel::KObjectPtr<kernel::KDriverObject>(driver));
        if (busDevice) {
            busDevice->name = "IsaPnpBus";
            busDevice->deviceType = kernel::DeviceType::Bus;
            Ke_PNP_EnumerateBus(busDevice);
        }
    }
}
