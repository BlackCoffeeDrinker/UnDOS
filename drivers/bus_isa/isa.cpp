

#include "isa_pnp.hpp"
#include <Kernel.hpp>
#include <kernel/resource.hpp>
#include <new.hpp>

namespace {

struct IsaBusContext {
  kstd::array<kernel::KDevicePtr<kernel::KDevice>, 32> children;
  size_t childCount = 0;
};

struct IsaCardContext {
  uint8_t serial[9];
  kstd::static_string<16> hardwareId;
  uint8_t csn;
  uint16_t ioBase = 0;  // ATA command-block I/O base (0 = not an ATA controller)
  uint16_t ctrlBase = 0;// ATA control-block base
  uint8_t irq = 0;      // ATA interrupt line
};

// The four standard legacy ATA/IDE channels. Adding or removing a channel is a
// one-line change here. Status port lives at io + 7.
struct AtaChannel {
  uint16_t io;
  uint16_t ctrl;
  uint8_t irq;
  const char *name;
};

constexpr AtaChannel kAtaChannels[] = {
    {0x1F0, 0x3F6, 14, "ATA1F0"},// Primary
    {0x170, 0x376, 15, "ATA170"},// Secondary
    {0x1E8, 0x3EE, 11, "ATA1E8"},// Tertiary
    {0x168, 0x36E, 10, "ATA168"},// Quaternary
};

struct IsaDmaAdapter : kernel::DmaOperation {
  uint32_t channel;
  kernel::DmaMode mode;
  bool autoInitialize;
};

IsaDmaAdapter isa_adapters[8];

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

kernel::VirtualAddress Isa_AllocateCommonBuffer(kernel::DmaOperation * /*self*/, size_t size, kernel::PhysicalAddress *physicalAddress, kernel::DmaAllocationFlags /*flags*/) {
  constexpr size_t PAGE_SIZE = 4096;
  const size_t count = (size + PAGE_SIZE - 1) / PAGE_SIZE;

  const kernel::PhysicalAddress phys = HAL_PMM_AllocateFramesDMA(count);
  if (!phys) return {0};

  auto &as = KE_VMM_GetKernelAddressSpace();
  const void *region = KE_VMM_AllocateRegion(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!region) {
    HAL_PMM_FreeFrames(phys, count);
    return {0};
  }
  kernel::VirtualAddress v_addr = kernel::VirtualAddress::from_ptr(region);

  for (size_t i = 0; i < count; ++i) {
    KE_VMM_MapPhysical(as, v_addr + (i * PAGE_SIZE), phys + (i * PAGE_SIZE), kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  }

  if (physicalAddress) *physicalAddress = phys;
  return v_addr;
}

void Isa_FreeCommonBuffer(kernel::DmaOperation * /*self*/, size_t size, kernel::PhysicalAddress physicalAddress, kernel::VirtualAddress virtualAddress) {
  constexpr size_t PAGE_SIZE = 4096;
  const size_t count = (size + PAGE_SIZE - 1) / PAGE_SIZE;

  auto &as = KE_VMM_GetKernelAddressSpace();
  KE_VMM_FreeRegion(as, virtualAddress.as_ptr());
  HAL_PMM_FreeFrames(physicalAddress, count);
}

bool Isa_AllocateAdapterChannel(kernel::DmaOperation *self, const kernel::KDevicePtr<kernel::KDevice> &deviceObject, uint32_t /*numberOfMapRegisters*/, kernel::cfunc<kernel::DmaAction(const kernel::KDevicePtr<kernel::KDevice> &deviceObject, kernel::VirtualAddress context, kernel::VirtualAddress mapRegisterBase, kernel::VirtualAddress reserved)> executionRoutine, kernel::VirtualAddress context) {
  // ISA doesn't have complex map registers. We just call the routine immediately.
  // In a real OS, we might check if the channel is currently in use.
  executionRoutine(deviceObject, context, kernel::VirtualAddress::from_ptr(self), kernel::VirtualAddress(0));
  return true;
}

void Isa_FreeAdapterChannel(kernel::DmaOperation * /*self*/, kernel::VirtualAddress /*mapRegisterBase*/) {
  // NOP
}

void Isa_FreeMapRegisters(kernel::DmaOperation * /*self*/, kernel::VirtualAddress /*mapRegisterBase*/, uint32_t /*numberOfMapRegisters*/) {
  // NOP
}

kernel::PhysicalAddress Isa_MapTransfer(kernel::DmaOperation * /*self*/, kernel::VirtualAddress /*mapRegisterBase*/, kernel::VirtualAddress virtualAddress, size_t *length, kernel::DmaDirection /*direction*/) {
  kernel::PhysicalAddress phys = HAL_VMM_GetPhysicalAddress(virtualAddress);

  // Check for 64K boundary crossing for ISA
  const auto addr = static_cast<uint32_t>(phys);
  const uint32_t end = addr + *length;
  if ((addr & 0xFFFF0000) != ((end - 1) & 0xFFFF0000)) {
    // If it crosses, truncate the length to the boundary
    *length = 0x10000 - (addr & 0xFFFF);
  }

  return phys;
}

bool Isa_GetScatterGatherList(kernel::DmaOperation *self, const kernel::KDevicePtr<kernel::KDevice> &deviceObject, kernel::VirtualAddress virtualAddress, size_t length, kernel::DmaDirection direction, kernel::cfunc<void(const kernel::KDevicePtr<kernel::KDevice> &deviceObject, kernel::VirtualAddress context, kernel::ScatterGatherList *sgList)> executionRoutine, kernel::VirtualAddress context) {
  auto *sgList = static_cast<kernel::ScatterGatherList *>(KE_Malloc(sizeof(kernel::ScatterGatherList)));
  if (!sgList) return false;

  sgList->reserved = 0;
  new (&sgList->elements) kernel::common::DoubleList<kernel::ScatterGatherElement>();

  size_t currentLength = length;
  kernel::PhysicalAddress phys = Isa_MapTransfer(self, kernel::VirtualAddress(0), virtualAddress, &currentLength, direction);

  auto *element = static_cast<kernel::ScatterGatherElement *>(KE_Malloc(sizeof(kernel::ScatterGatherElement)));
  if (!element) {
    sgList->elements.~DoubleList();
    KE_Free(sgList);
    return false;
  }

  element->address = phys;
  element->length = static_cast<uint32_t>(currentLength);
  element->reserved = 0;

  sgList->elements.push_back(element);

  executionRoutine(deviceObject, context, sgList);
  return true;
}

void Isa_PutScatterGatherList(kernel::DmaOperation * /*self*/, kernel::ScatterGatherList *sgList, kernel::DmaDirection /*direction*/) {
  if (!sgList) return;

  while (!sgList->elements.empty()) {
    auto *element = &sgList->elements.front();
    sgList->elements.pop_front();
    KE_Free(element);
  }

  sgList->elements.~DoubleList();
  KE_Free(sgList);
}

size_t Isa_GetDmaAlignment(kernel::DmaOperation * /*self*/) {
  return 1;
}

void Isa_SetupTransfer(kernel::DmaOperation *self, kernel::PhysicalAddress address, size_t count, kernel::DmaDirection direction) {
  auto *adapter = static_cast<IsaDmaAdapter *>(self);
  const auto channel = static_cast<uint8_t>(adapter->channel);
  if (channel >= 8) return;

  const bool is16Bit = (channel >= 4);
  const uint16_t port_base = is16Bit ? static_cast<uint16_t>(0xC0 + ((channel - 4) * 4)) : static_cast<uint16_t>(channel * 2);
  const uint16_t port_count = is16Bit ? static_cast<uint16_t>(port_base + 2) : static_cast<uint16_t>(port_base + 1);
  const uint16_t port_flipflop = is16Bit ? 0xD8 : 0x0C;
  const uint16_t port_mask = is16Bit ? 0xD4 : 0x0A;
  const uint16_t port_mode = is16Bit ? 0xD6 : 0x0B;
  const auto channel_bit = static_cast<uint8_t>(is16Bit ? (channel - 4) : channel);

  const auto addr = static_cast<uint32_t>(address);
  const auto dma_count = static_cast<uint16_t>(is16Bit ? (count / 2) - 1 : count - 1);
  const auto dma_addr = static_cast<uint16_t>(is16Bit ? (addr >> 1) & 0xFFFF : addr & 0xFFFF);

  uint8_t mode_byte = channel_bit;
  if (direction == kernel::DmaDirection::Read) {
    mode_byte |= 0x04;// Write to memory
  } else {
    mode_byte |= 0x08;// Read from memory
  }

  if (adapter->autoInitialize) mode_byte |= 0x10;

  switch (adapter->mode) {
    case kernel::DmaMode::Demand:
      mode_byte |= 0x00;
      break;
    case kernel::DmaMode::Single:
      mode_byte |= 0x40;
      break;
    case kernel::DmaMode::Block:
      mode_byte |= 0x80;
      break;
    case kernel::DmaMode::Cascade:
      mode_byte |= 0xC0;
      break;
  }

  // Mask channel
  HAL_IO_Out8(port_mask, static_cast<uint8_t>(0x04 | channel_bit));

  // Reset flip-flop
  HAL_IO_Out8(port_flipflop, 0x00);

  // Set mode
  HAL_IO_Out8(port_mode, mode_byte);

  // Set address
  HAL_IO_Out8(port_base, static_cast<uint8_t>(dma_addr & 0xFF));
  HAL_IO_Out8(port_base, static_cast<uint8_t>((dma_addr >> 8) & 0xFF));

  // Set page register
  constexpr uint8_t page_ports[] = {0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A};
  HAL_IO_Out8(page_ports[channel], static_cast<uint8_t>((addr >> 16) & 0xFF));

  // Reset flip-flop for count
  HAL_IO_Out8(port_flipflop, 0x00);

  // Set count
  HAL_IO_Out8(port_count, static_cast<uint8_t>(dma_count & 0xFF));
  HAL_IO_Out8(port_count, static_cast<uint8_t>((dma_count >> 8) & 0xFF));

  // Unmask channel
  HAL_IO_Out8(port_mask, channel_bit);
}

void Isa_StopTransfer(kernel::DmaOperation *self) {
  auto *adapter = static_cast<IsaDmaAdapter *>(self);
  const auto channel = static_cast<uint8_t>(adapter->channel);
  if (channel >= 8) return;
  const uint16_t port_mask = (channel < 4) ? static_cast<uint16_t>(0x0A) : static_cast<uint16_t>(0xD4);
  const auto channel_bit = static_cast<uint8_t>((channel < 4) ? channel : (channel - 4));
  HAL_IO_Out8(port_mask, static_cast<uint8_t>(0x04 | channel_bit));
}

size_t Isa_ReadCounter(kernel::DmaOperation *self) {
  auto *adapter = static_cast<IsaDmaAdapter *>(self);
  const auto channel = static_cast<uint8_t>(adapter->channel);
  if (channel >= 8) return 0;
  const bool is16Bit = (channel >= 4);
  const uint16_t port_count = is16Bit ? static_cast<uint16_t>(0xC2 + ((channel - 4) * 4)) : static_cast<uint16_t>(0x01 + (channel * 2));
  const uint16_t port_flipflop = is16Bit ? 0xD8 : 0x0C;

  HAL_IO_Out8(port_flipflop, 0x00);
  const uint16_t low = HAL_IO_In8(port_count);
  const uint16_t high = HAL_IO_In8(port_count);
  const auto val = static_cast<uint16_t>((high << 8) | low);

  const auto real_val = static_cast<uint32_t>(is16Bit ? (val + 1) * 2 : val + 1);
  return real_val;
}

kernel::DmaOperation *Isa_GetDmaAdapter(const kernel::KDevicePtr<kernel::KDevice> & /*device*/, const kernel::DmaDescription &description) {
  if (description.channel >= 8) return nullptr;

  auto &adapter = isa_adapters[description.channel];
  adapter.channel = description.channel;
  adapter.mode = description.mode;
  adapter.autoInitialize = description.autoInitialize;

  adapter.AllocateCommonBuffer = Isa_AllocateCommonBuffer;
  adapter.FreeCommonBuffer = Isa_FreeCommonBuffer;
  adapter.AllocateAdapterChannel = Isa_AllocateAdapterChannel;
  adapter.FreeAdapterChannel = Isa_FreeAdapterChannel;
  adapter.FreeMapRegisters = Isa_FreeMapRegisters;
  adapter.MapTransfer = Isa_MapTransfer;
  adapter.GetScatterGatherList = Isa_GetScatterGatherList;
  adapter.PutScatterGatherList = Isa_PutScatterGatherList;
  adapter.GetDmaAlignment = Isa_GetDmaAlignment;
  adapter.SetupTransfer = Isa_SetupTransfer;
  adapter.StopTransfer = Isa_StopTransfer;
  adapter.ReadCounter = Isa_ReadCounter;

  return &adapter;
}

void Probe_PnP(const kernel::KDevicePtr<kernel::KDevice> &busPdo, IsaBusContext *ctx) {
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
                                      "" /* unnamed PDO */,
                                      kernel::device_type::Unknown);
      if (child) {
        auto *childCtx = child->deviceExtension.as<IsaCardContext>();
        for (int i = 0; i < 9; i++) childCtx->serial[i] = serial[i];
        childCtx->csn = csn;
        // Simple EISA ID formatting (PNPXXXX)
        childCtx->hardwareId = "PNPSAMP";
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

void Isa_EnumerateDevices(const kernel::KDevicePtr<kernel::KDevice> &busPdo) {
  auto *ctx = busPdo->deviceExtension.as<IsaBusContext>();
  if (!ctx) return;

  ctx->childCount = 0;

  Probe_PnP(busPdo, ctx);

  // Legacy ATA controller probing: check the four standard channels and create
  // one PDO per controller that responds (status port at base+7 != 0xFF). Each
  // PDO records its channel's fixed ports/IRQ so the bus can report them.
  for (const auto &channel: kAtaChannels) {
    if (ctx->childCount >= ctx->children.size()) break;
    if (HAL_IO_In8(static_cast<uint16_t>(channel.io + 7)) == 0xFF) continue;

    early_print_fmt("ISA: Found legacy ATA controller at 0x{x} (ctrl 0x{x}, IRQ {})\r\n",
                    channel.io, channel.ctrl, channel.irq);

    if (const auto child = KE_IO_CreateDevice(busPdo->driverObject,
                                              sizeof(IsaCardContext),
                                              channel.name,
                                              kernel::device_type::DiskController)) {
      auto *childCtx = child->deviceExtension.as<IsaCardContext>();
      childCtx->hardwareId = "disk_ata";
      childCtx->ioBase = channel.io;
      childCtx->ctrlBase = channel.ctrl;
      childCtx->irq = channel.irq;
      ctx->children[ctx->childCount++] = child;
    }
  }
}

size_t Isa_GetChildCount(const kernel::KDevicePtr<kernel::KDevice> &busPdo) {
  auto *ctx = busPdo->deviceExtension.as<IsaBusContext>();
  return ctx ? ctx->childCount : 0;
}

kernel::KDevicePtr<kernel::KDevice> Isa_GetChild(const kernel::KDevicePtr<kernel::KDevice> &busPdo, size_t index) {
  if (auto *ctx = busPdo->deviceExtension.as<IsaBusContext>();
      ctx && index < ctx->childCount) {
    return ctx->children[index];
  }
  return nullptr;
}

kstd::string_view Isa_GetHardwareId(const kernel::KDevicePtr<kernel::KDevice> &childPdo) {
  const auto *ctx = childPdo->deviceExtension.as<IsaCardContext>();
  if (ctx) {
    return ctx->hardwareId;
  }
  return "UNKNOWN_ISA_DEVICE";
}

// Reports the fixed resources of a detected ATA controller child: its command
// I/O range (base, 8 ports), its control port (1), and its IRQ (shared, since
// real hardware may cascade IRQ 14/15). Non-ATA children have no requirements.
kernel::IoResourceRequirementsList *Isa_QueryResourceRequirements(const kernel::KDevicePtr<kernel::KDevice> &childPdo) {
  const auto *ctx = childPdo->deviceExtension.as<IsaCardContext>();
  if (!ctx || ctx->hardwareId != "disk_ata" || ctx->ioBase == 0) return nullptr;

  auto *req = KE_RES_CreateRequirementsList(kernel::device_type::DiskController);
  if (!req) return nullptr;
  auto *alt = KE_RES_AddAlternative(req);
  if (!alt) {
    KE_RES_FreeRequirementsList(req);
    return nullptr;
  }
  KE_RES_AddPort(alt, ctx->ioBase, 8, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  KE_RES_AddPort(alt, ctx->ctrlBase, 1, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  KE_RES_AddInterrupt(alt, ctx->irq, kernel::ResourceOption::Fixed, kernel::ResourceShare::Shared);
  return req;
}

bool IsaBus_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver,
                      const kernel::KDevicePtr<kernel::KDevice> &targetPdo) {
  // Create our unnamed Bus FDO and stack it on top of the target PDO. The
  // target may be the HAL's legacy root PDO or (in the future) a PCI-to-ISA
  // bridge PDO; the driver behaves identically either way
  kstd::static_string<64> isaBusName;
  kstd::format(isaBusName, "IsaFdo{}", targetPdo->Name());

  const auto isaBusFdo = KE_IO_CreateBusWithContext<IsaBusContext>(driver, isaBusName);
  if (!isaBusFdo) return false;

  KE_IO_AttachFilterDevice(isaBusFdo, targetPdo);
  return true;
}
}// namespace

UNDOS_DRIVER_ENTRY {
  driver->AddDevice = IsaBus_AddDevice;
  driver->EnumerateDevices = Isa_EnumerateDevices;
  driver->GetChildCount = Isa_GetChildCount;
  driver->GetChild = Isa_GetChild;
  driver->GetHardwareId = Isa_GetHardwareId;
  driver->GetDmaAdapter = Isa_GetDmaAdapter;
  driver->QueryResourceRequirements = Isa_QueryResourceRequirements;
}
