
#include <Kernel.hpp>
#include <array.hpp>
#include <kernel/dma.hpp>
#include <kernel/resource.hpp>
#include <span.hpp>

namespace {

constexpr uint32_t kAtaSectorSize = 512;

// ATA command-block register offsets from the I/O base.
constexpr uint8_t kRegData = 0;
constexpr uint8_t kRegFeatures = 1;
constexpr uint8_t kRegSectorCount = 2;
constexpr uint8_t kRegLbaLow = 3;
constexpr uint8_t kRegLbaMid = 4;
constexpr uint8_t kRegLbaHigh = 5;
constexpr uint8_t kRegDrive = 6;
constexpr uint8_t kRegStatusCommand = 7;

// Absolute port for a command-block register relative to the I/O base.
inline uint16_t AtaPort(uint16_t ioBase, uint8_t reg) {
  return static_cast<uint16_t>(ioBase + reg);
}

static kernel::KernelError AtaWait(uint16_t io) {
  // 400ns delay: read status port 4 times
  for (int i = 0; i < 4; i++) {
    HAL_IO_In8(AtaPort(io, kRegStatusCommand));
  }

  while (true) {
    const uint8_t status = HAL_IO_In8(AtaPort(io, kRegStatusCommand));

    if (status & 0x01) return kernel::KernelError::DeviceError;                      // ERR
    if (!(status & 0x80) && (status & 0x08)) return kernel::KernelError::Success;// !BSY && DRQ
  }
}


struct BusContext {
  kernel::DmaOperation *dmaAdapter;
  kernel::VirtualAddress commonBuffer;
  kernel::PhysicalAddress commonBufferPhys;

  uint16_t ioBase;  // command-block base, resolved from the grant (0 = unstarted)
  uint16_t ctrlBase;// control-block base, resolved from the grant
  uint32_t irq;     // assigned interrupt line
};

struct AtaContext {
  kernel::KDevicePtr<kernel::KDevice> pdo;
  bool isAtapi;

  uint32_t sectorCount;// LBA28 addressable sectors, from IDENTIFY (0 = unknown)
  uint32_t sectorSize; // bytes per sector (512)

  kstd::static_string<41> model;
  kstd::static_string<21> serial;
};

template<size_t N>
kstd::static_string<N> Ata_ExtractString(const kstd::span<uint16_t> &identify,
                                         size_t startWord,
                                         size_t wordCount) {
  kstd::static_string<N> out;

  // Safety guard
  if (startWord + wordCount > identify.size())
    wordCount = identify.size() - startWord;

  for (size_t i = 0; i < wordCount; ++i) {
    const uint16_t w = identify[startWord + i];

    // ATA strings are big‑endian pairs of ASCII bytes
    char hi = static_cast<char>((w >> 8) & 0xFF);
    char lo = static_cast<char>((w >> 0) & 0xFF);

    if (hi != ' ' && out.size() < out.capacity())
      out.push_back(hi);
    if (lo != ' ' && out.size() < out.capacity())
      out.push_back(lo);
  }

  // Trim trailing spaces (ATA pads with spaces)
  while (!out.empty() && out.back() == ' ')
    out.pop_back();

  return out;
}

kernel::KernelError Ata_ProbeDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo,
                     const kernel::KDevicePtr<kernel::KDeviceBus> &fdo,
                     uint16_t ioBase,
                     const bool isMaster) {
  const uint8_t driveSel = isMaster ? 0xA0 : 0xB0;

  // 1. Select device
  HAL_IO_Out8(AtaPort(ioBase, kRegDrive), driveSel);
  HAL_IO_Delay();

  // 2. Clear registers
  HAL_IO_Out8(AtaPort(ioBase, kRegSectorCount), 0);
  HAL_IO_Out8(AtaPort(ioBase, kRegLbaLow), 0);
  HAL_IO_Out8(AtaPort(ioBase, kRegLbaMid), 0);
  HAL_IO_Out8(AtaPort(ioBase, kRegLbaHigh), 0);

  // 3. Issue IDENTIFY DEVICE
  HAL_IO_Out8(AtaPort(ioBase, kRegStatusCommand), 0xEC);
  HAL_IO_Delay();

  // 4. Poll BSY until clear
  uint8_t status = 0;
  for (int i = 0; i < 100000; ++i) {
    status = HAL_IO_In8(AtaPort(ioBase, kRegStatusCommand));
    if (!(status & 0x80)) break;// BSY cleared
    HAL_IO_Delay();
  }

  if (status & 0x80) {
    KE_Print("ATA: BSY stuck, no device");
    return kernel::KernelError::Timeout;
  }

  // 5. Check for floating bus / no device
  if (status == 0x00 || status == 0xFF) {
    KE_Print("ATA: No device present");
    return kernel::KernelError::NotFound;
  }

  // 6. Check ATAPI signature
  const uint8_t lbaMid = HAL_IO_In8(AtaPort(ioBase, kRegLbaMid));
  const uint8_t lbaHigh = HAL_IO_In8(AtaPort(ioBase, kRegLbaHigh));

  const bool isAtapi = (lbaMid == 0x14 && lbaHigh == 0xEB);
  if (isAtapi) {
    HAL_IO_Out8(AtaPort(ioBase, kRegStatusCommand), 0xA1);// IDENTIFY PACKET
    HAL_IO_Delay();
  }

  // 7. Wait for DRQ or ERR
  for (int i = 0; i < 100000; ++i) {
    status = HAL_IO_In8(AtaPort(ioBase, kRegStatusCommand));
    if (status & 0x01) break;// ERR
    if (status & 0x08) break;// DRQ
    HAL_IO_Delay();
  }

  if (status & 0x01) {
    KE_Print("ATA: IDENTIFY ERR");
    return kernel::KernelError::DeviceError;
  }
  if (!(status & 0x08)) {
    KE_Print("ATA: IDENTIFY no DRQ");
    return kernel::KernelError::DeviceNotReady;
  }

  // 8. Read IDENTIFY data
  kstd::array<uint16_t, 256> identify{};
  for (unsigned short &i: identify) {
    i = HAL_IO_In16(AtaPort(ioBase, kRegData));
  }

  // Extract model + serial
  auto model = Ata_ExtractString<41>(identify, 27, 20);
  auto serial = Ata_ExtractString<21>(identify, 10, 10);

  KE_Print("ATA: Found disk: model='{}' serial='{}'", model, serial);

  // 10. Create Disk PDO
  kstd::static_string<64> diskName;
  kstd::format(diskName, "PhysicalDisk{}", serial);

  const auto created = KE_IO_CreateBusWithContext<AtaContext>(
      fdo->driverObject, diskName);
  if (!created) {
    KE_Print("ATA: Failed to create Disk PDO");
    return created.error();
  }
  const auto &disk = *created;
  disk->deviceType = kernel::device_type::Disk;

  // 11. Fill deviceExtension
  const auto dctx = disk->deviceExtension.as<AtaContext>();
  dctx->pdo = pdo;
  dctx->model = model;
  dctx->serial = serial;
  dctx->isAtapi = isAtapi;
  disk->hardwareId = "disk";
  // LBA28 total addressable sectors live in words 60-61 of IDENTIFY data.
  dctx->sectorCount = isAtapi
                          ? 0
                          : (static_cast<uint32_t>(identify[60]) | (static_cast<uint32_t>(identify[61]) << 16));
  dctx->sectorSize = isAtapi ? 2048 : kAtaSectorSize;

  // 12. Report the Disk PDO to PnP
  return KE_PNP_ReportNewDevice(fdo, disk);
}


kernel::KernelError Ata_Read(const kernel::KDevicePtr<kernel::KDevice> &device, uint64_t offset, kstd::span<uint8_t> buffer, size_t &transferred) {
  transferred = 0;
  if (device->deviceType != kernel::device_type::Disk) return kernel::KernelError::Unsupported;

  const auto *dctx = device->deviceExtension.as<AtaContext>();
  if (!dctx || dctx->isAtapi) return kernel::KernelError::Unsupported;
  const auto fdo = dctx->pdo->attachedDevice.As<kernel::KDeviceBus>();
  if (!fdo) return kernel::KernelError::DeviceError;
  const auto *bctx = fdo->deviceExtension.as<BusContext>();
  if (!bctx || !bctx->ioBase) return kernel::KernelError::DeviceError;

  const uint16_t io = bctx->ioBase;

  const size_t sector_count = (buffer.size() + kAtaSectorSize - 1) / kAtaSectorSize;

  for (size_t s = 0; s < sector_count; ++s) {
    const auto lba = static_cast<uint32_t>(offset / kAtaSectorSize) + static_cast<uint32_t>(s);

    // LBA28 PIO
    HAL_IO_Out8(AtaPort(io, kRegDrive), 0xE0 | ((lba >> 24) & 0x0F));
    HAL_IO_Out8(AtaPort(io, kRegFeatures), 0);
    HAL_IO_Out8(AtaPort(io, kRegSectorCount), 1);
    HAL_IO_Out8(AtaPort(io, kRegLbaLow), lba & 0xFF);
    HAL_IO_Out8(AtaPort(io, kRegLbaMid), (lba >> 8) & 0xFF);
    HAL_IO_Out8(AtaPort(io, kRegLbaHigh), (lba >> 16) & 0xFF);
    HAL_IO_Out8(AtaPort(io, kRegStatusCommand), 0x20);// READ SECTORS

    // Wait
    if (AtaWait(io) != kernel::KernelError::Success) {
      return transferred > 0 ? kernel::KernelError::Success : kernel::KernelError::DeviceError;
    }

    // Read 512 bytes (256 words) for this sector into the buffer at the
    // correct offset.
    const size_t sector_base = s * kAtaSectorSize;
    for (uint32_t i = 0; i < 256; i++) {
      const uint16_t data = HAL_IO_In16(AtaPort(io, kRegData));
      const size_t byte0 = sector_base + i * 2;
      const size_t byte1 = byte0 + 1;

      if (byte0 < buffer.size()) {
        buffer[byte0] = static_cast<uint8_t>(data & 0xFF);
      }

      if (byte1 < buffer.size()) {
        buffer[byte1] = static_cast<uint8_t>(data >> 8);
      }
    }

    transferred += kstd::min(static_cast<size_t>(kAtaSectorSize), buffer.size() - sector_base);
  }

  return kernel::KernelError::Success;
}

kernel::KernelError Ata_Write([[maybe_unused]] const kernel::KDevicePtr<kernel::KDevice> &device, [[maybe_unused]] uint64_t offset, [[maybe_unused]] kstd::span<const uint8_t> buffer, [[maybe_unused]] size_t &transferred) {
  return kernel::KernelError::Unsupported;
}

void Ata_StartDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  KE_Print("Ata_StartDevice with PDO: {}", pdo->Name());

  const auto fdo = pdo->attachedDevice.As<kernel::KDeviceBus>();
  if (!fdo) {
    KE_Print("ATA: No FDO attached; cannot start");
    return;
  }

  const auto ctx = fdo->deviceExtension.as<BusContext>();
  if (!ctx) {
    KE_Print("ATA: Missing BusContext in deviceExtension");
    return;
  }

  // Resolve resources assigned to this controller (via the PDO)
  const auto *ioRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Port, 0);
  const auto *ctrlRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Port, 1);
  const auto *irqRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Interrupt, 0);

  if (!ioRes) {
    KE_Print("ATA: No I/O resources assigned; cannot start");
    return;
  }

  ctx->ioBase = static_cast<uint16_t>(ioRes->start);
  ctx->ctrlBase = ctrlRes ? static_cast<uint16_t>(ctrlRes->start) : 0;
  ctx->irq = irqRes ? irqRes->start : 0;

  const uint16_t io = ctx->ioBase;
  const uint16_t ctrl = ctx->ctrlBase;

  KE_Print("ATA: Using I/O base 0x{x}, control 0x{x}, IRQ {}",
                  ctx->ioBase, ctx->ctrlBase, ctx->irq);
  KE_Print("ATA: Starting device identification (master)...");

  // --- Optional: soft reset on the channel ---
  if (ctrl) {
    // Set SRST (bit 2) in control register, then clear it
    HAL_IO_Out8(ctrl, 0x04);
    HAL_IO_Delay();
    HAL_IO_Out8(ctrl, 0x00);
    // Wait a bit for BSY to clear
    for (int i = 0; i < 100000; ++i) {
      const uint8_t s = HAL_IO_In8(AtaPort(io, kRegStatusCommand));
      if (!(s & 0x80)) break;// BSY cleared
      HAL_IO_Delay();
    }
  }

  // An absent drive is expected on most channels, so probe failures are not fatal.
  (void) Ata_ProbeDevice(pdo, fdo, ctx->ioBase, true); // master
  (void) Ata_ProbeDevice(pdo, fdo, ctx->ioBase, false);// slave
}

kernel::KernelError Ata_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = KE_IO_CreateBusWithContext<BusContext>(driver, {});
  if (!fdo) return fdo.error();

  const auto attached = KE_IO_AttachFilterDevice(*fdo, pdo);
  if (!attached) return attached.error();
  return kernel::KernelError::Success;
}

}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = Ata_AddDevice;
  driver->StartDevice = Ata_StartDevice;
  driver->Read = Ata_Read;
  driver->Write = Ata_Write;
}
