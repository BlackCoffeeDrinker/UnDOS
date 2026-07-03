

#include "isa_pnp.hpp"

#include <Kernel.hpp>

namespace {

struct IsaBusContext {
  kernel::KObjectPtr<kernel::KPhysicalDeviceObject> children[32];
  size_t childCount = 0;
};

struct IsaCardContext {
  uint8_t serial[9];
  char hardwareId[16];
  uint8_t csn;
};

// I/O Ports
constexpr uint16_t PNP_ADDRESS_PORT = 0x0279;
constexpr uint16_t PNP_WRITE_DATA_PORT = 0x0A79;
constexpr uint16_t PNP_READ_DATA_PORT = 0x0203;

// Config Control Bits
constexpr uint8_t CONTROL_RESET_CSN = 0x04;   // Bit 2
constexpr uint8_t CONTROL_WAIT_FOR_KEY = 0x02;// Bit 1
constexpr uint8_t CONTROL_RESET_DRV = 0x01;   // Bit 0

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

void WriteConfigReg(PnpRegister reg) {
  HAL_IO_Out8(PNP_ADDRESS_PORT, static_cast<uint8_t>(reg));
}

void WriteData(uint8_t val) {
  HAL_IO_Out8(PNP_WRITE_DATA_PORT, val);
}

uint8_t ReadData() {
  return HAL_IO_In8(PNP_READ_DATA_PORT);
}

void SendInitiationKey() {
  uint8_t val = 0x6A;
  HAL_IO_Out8(PNP_ADDRESS_PORT, 0);
  HAL_IO_Out8(PNP_ADDRESS_PORT, 0);
  for (int i = 0; i < 32; i++) {
    HAL_IO_Out8(PNP_ADDRESS_PORT, val);
    const uint8_t bit = ((val >> 0) ^ (val >> 1)) & 1;
    val = (val >> 1) | (bit << 7);
  }
}

bool Isolate(uint8_t (&serial)[9]) {
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

void* Isa_AllocateCommonBuffer(kernel::DmaOperation* /*self*/, size_t /*size*/, kernel::PhysicalAddress* physicalAddress, bool /*cacheEnabled*/) {
  // TODO: Implement contiguous allocation < 16MB
  if (physicalAddress) *physicalAddress = 0;
  return nullptr;
}

void Isa_FreeCommonBuffer(kernel::DmaOperation* /*self*/, size_t /*size*/, kernel::PhysicalAddress /*physicalAddress*/, void* /*virtualAddress*/) {
  // TODO
}

kernel::DmaOperation* Isa_GetDmaAdapter(const kernel::KObjectPtr<kernel::KPhysicalDeviceObject>& /*device*/) {
  static kernel::DmaOperation adapter;
  static bool initialized = false;
  if (!initialized) {
    adapter.AllocateCommonBuffer = Isa_AllocateCommonBuffer;
    adapter.FreeCommonBuffer = Isa_FreeCommonBuffer;
    initialized = true;
  }
  return &adapter;
}

void Isa_EnumerateDevices(const kernel::KObjectPtr<kernel::KPhysicalDeviceObject> &busPdo) {
  auto *ctx = busPdo->deviceExtension.as<IsaBusContext>();
  if (!ctx) return;

  ctx->childCount = 0;

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
  while (csn < 32 && ctx->childCount < 32) {
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

      auto child = KE_IO_CreateDevice(busPdo->driverObject,
                                      sizeof(IsaCardContext),
                                      "IsaPnpCard",
                                      kernel::device_type::Unknown);
      if (child) {
        auto *childCtx = child->deviceExtension.as<IsaCardContext>();
        for (int i = 0; i < 9; i++) childCtx->serial[i] = serial[i];
        childCtx->csn = csn;
        // Simple EISA ID formatting (PNPXXXX)
        childCtx->hardwareId[0] = 'P';
        childCtx->hardwareId[1] = 'N';
        childCtx->hardwareId[2] = 'P';
        childCtx->hardwareId[3] = 'S';
        childCtx->hardwareId[4] = 'A';
        childCtx->hardwareId[5] = 'M';
        childCtx->hardwareId[6] = 'P';
        childCtx->hardwareId[7] = '\0';

        ctx->children[ctx->childCount++] = child;
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

size_t Isa_GetChildCount(const kernel::KObjectPtr<kernel::KPhysicalDeviceObject> &busPdo) {
  auto *ctx = busPdo->deviceExtension.as<IsaBusContext>();
  return ctx ? ctx->childCount : 0;
}

kernel::KObjectPtr<kernel::KPhysicalDeviceObject> Isa_GetChild(const kernel::KObjectPtr<kernel::KPhysicalDeviceObject> &busPdo, size_t index) {
  auto *ctx = busPdo->deviceExtension.as<IsaBusContext>();
  if (ctx && index < ctx->childCount) {
    return ctx->children[index];
  }
  return nullptr;
}

kstd::string_view Isa_GetHardwareId(const kernel::KObjectPtr<kernel::KPhysicalDeviceObject> &childPdo) {
  auto *ctx = childPdo->deviceExtension.as<IsaCardContext>();
  if (ctx) {
    return ctx->hardwareId;
  }
  return "UNKNOWN_ISA_DEVICE";
}

void HandleEvent(const kernel::KObjectPtr<kernel::KDriverObject> & /*driver*/, const kernel::KEvent &event) {
  (void) event;
}


} // namespace

extern "C" void DriverEntry(kernel::KObjectPtr<kernel::KDriverObject> &driver) {
  early_print("DriverEntry called!\r\n");
  driver->eventHandler = HandleEvent;
  driver->EnumerateDevices = Isa_EnumerateDevices;
  driver->GetChildCount = Isa_GetChildCount;
  driver->GetChild = Isa_GetChild;
  driver->GetHardwareId = Isa_GetHardwareId;
  driver->GetDmaAdapter = Isa_GetDmaAdapter;

  KE_IO_CreateDevice(driver,
                     sizeof(IsaBusContext),
                     "IsaBus",
                     kernel::device_type::Bus);
}
