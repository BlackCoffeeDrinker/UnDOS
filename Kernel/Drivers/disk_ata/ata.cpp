
#include <Kernel.hpp>
#include <array.hpp>
#include <kernel/dma.hpp>
#include <kernel/resource.hpp>
#include <kernel/time.hpp>
#include <span.hpp>
#include <kernel/mutex.hpp>
#include <mutex.hpp>

namespace {

constexpr uint32_t kAtaSectorSize = 512;
constexpr uint32_t kAtapiSectorSize = 2048;
constexpr uint32_t kMaxSectorSize = kAtapiSectorSize;
// ATAPI READ(10) sectors per PACKET command.
constexpr uint16_t kAtapiMaxBatch = 32;

// Minimum resource lengths (Docs/Storage.md, "ATA channel resources").
constexpr uint32_t kCommandBlockLength = 8;
constexpr uint32_t kControlBlockLength = 1;

// Device control register values: SRST (bit 2) and nIEN (bit 1).
constexpr uint8_t kControlReset = 0x06;
constexpr uint8_t kControlIdle = 0x02;

// ATA command-block register offsets from the I/O base.
constexpr uint8_t kRegData = 0;
constexpr uint8_t kRegFeatures = 1; // write: features, read: error
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

static kstd::error_code AtaWait(uint16_t io, bool data = true, bool checkError = true) {
  // 400ns delay: read status port 4 times
  for (int i = 0; i < 4; i++) {
    HAL_IO_In8(AtaPort(io, kRegStatusCommand));
  }

  for (uint32_t poll = 0; poll < 1000000; ++poll) {
    const uint8_t status = HAL_IO_In8(AtaPort(io, kRegStatusCommand));
    if (!status || status == 0xff) return kernel::KernelError::DeviceNotReady;
    if (status & 0x80) continue;
    if (checkError && (status & 0x21)) return kernel::KernelError::DeviceError;
    if (!data || (status & 0x08)) return {};
  }
  return kernel::KernelError::Timeout;
}


struct BusContext {
  kernel::DmaOperation *dmaAdapter;
  kernel::VirtualAddress commonBuffer;
  kernel::PhysicalAddress commonBufferPhys;

  uint16_t ioBase;  // command-block base, resolved from the grant (0 = unstarted)
  uint16_t ctrlBase;// control-block base, resolved from the grant
  uint32_t irq;     // assigned interrupt line
  bool unusable{false};
  kernel::Mutex ioMutex;
};

struct AtaContext {
  kernel::KDevicePtr<kernel::KDevice> pdo;
  bool isAtapi;
  bool isMaster;

  // TODO(LBA48): support 48-bit LBA (IDENTIFY words 100-103, READ SECTORS EXT); disks are capped at 128 GiB.
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

struct AtaReadWaitResult {
  kstd::error_code status{};
  bool uncertain{false};
};

AtaReadWaitResult AtaWaitReadStatus(uint16_t io, bool expectData) {
  uint8_t status = 0;
  for (uint32_t poll = 0; poll < 1000000; ++poll) {
    status = HAL_IO_In8(AtaPort(io, kRegStatusCommand));
    if (!status || status == 0xff) return {kernel::KernelError::DeviceNotReady, true};
    if (status & 0x80) continue;
    if (status & 0x21) {
      return {kernel::KernelError::DeviceError, (status & 0x08) != 0};
    }
    if (expectData ? (status & 0x08) != 0 : (status & 0x08) == 0) return {};
  }
  return {kernel::KernelError::Timeout, true};
}

// Waits for `ticks` observed advances of the executive clock (100 Hz PIT).
// The first tick can occur immediately after the caller's register write, so
// callers ask for one more tick than the minimum they need.
// The 64-bit clock is updated by IRQ and read non-atomically on i386, so a
// sample can be torn. Only the low 32 bits (each loaded atomically) may
// count ticks; the high 32 bits must not affect the decision.
bool AtaWaitObservedTicks(unsigned ticks) {
  auto lastTime = static_cast<uint32_t>(KE_TIME_GetSystemTime().count());
  unsigned elapsedTicks = 0;
  for (uint32_t poll = 0; poll < 1000000 && elapsedTicks < ticks; ++poll) {
    HAL_IO_Delay();
    const auto now = static_cast<uint32_t>(KE_TIME_GetSystemTime().count());
    if (now != lastTime) {
      lastTime = now;
      ++elapsedTicks;
    }
  }
  return elapsedTicks >= ticks;
}

// Pulses SRST on the channel: asserted for two observed ticks (>= 10 ms,
// well over the 5 us minimum), then released with nIEN kept set, then two
// more observed ticks (>= 10 ms, over the ~2 ms the devices need) before the
// caller polls status. A stopped clock cannot establish either interval, so
// the channel is poisoned instead of trusting an unproven reset.
bool AtaSoftReset(BusContext &ctx) {
  if (!ctx.ctrlBase) {
    ctx.unusable = true;
    return false;
  }

  HAL_IO_Out8(ctx.ctrlBase, kControlReset);
  const bool held = AtaWaitObservedTicks(2);
  HAL_IO_Out8(ctx.ctrlBase, kControlIdle);
  if (!held || !AtaWaitObservedTicks(2)) {
    ctx.unusable = true;
    return false;
  }
  return true;
}

void AtaRecoverChannel(BusContext &ctx) {
  if (!AtaSoftReset(ctx)) return;

  for (int i = 0; i < 4; ++i) (void) HAL_IO_In8(AtaPort(ctx.ioBase, kRegStatusCommand));
  for (uint32_t poll = 0; poll < 100000; ++poll) {
    const uint8_t status = HAL_IO_In8(AtaPort(ctx.ioBase, kRegStatusCommand));
    if (status && status != 0xff && !(status & (0x80 | 0x08 | 0x21))) return;
    HAL_IO_Delay();
  }
  ctx.unusable = true;
}

// Runs one ATAPI PACKET command with a PIO data-in phase.
// `expected` is the exact number of bytes the command must return; every DRQ
// phase must report a byte count that is a non-zero multiple of `unit` and
// fits in what remains. `transferred` only counts bytes of completed phases.
kstd::error_code Atapi_PacketRead(BusContext &ctx, bool isMaster, const uint8_t (&cdb)[12],
    uint8_t *output, size_t expected, size_t unit, size_t &transferred) {
  const uint16_t io = ctx.ioBase;
  HAL_IO_Out8(AtaPort(io, kRegDrive), isMaster ? 0xA0 : 0xB0);
  for (int i = 0; i < 4; ++i) (void) HAL_IO_In8(AtaPort(io, kRegStatusCommand));
  if (const auto status = AtaWait(io, false, false); status) return status;

  const uint16_t byteLimit = static_cast<uint16_t>(kstd::min<size_t>(expected, kAtapiSectorSize));
  HAL_IO_Out8(AtaPort(io, kRegFeatures), 0);// PIO
  HAL_IO_Out8(AtaPort(io, kRegLbaMid), static_cast<uint8_t>(byteLimit & 0xff));
  HAL_IO_Out8(AtaPort(io, kRegLbaHigh), static_cast<uint8_t>((byteLimit >> 8) & 0xff));
  HAL_IO_Out8(AtaPort(io, kRegStatusCommand), 0xA0);// PACKET

  auto fail = [&](const AtaReadWaitResult &result) -> kstd::error_code {
    if (result.status == kernel::KernelError::DeviceError && !result.uncertain) {
      // CHECK CONDITION: the sense key is in the error register high nibble.
      const uint8_t senseKey = HAL_IO_In8(AtaPort(io, kRegFeatures)) >> 4;
      if (senseKey == 0x2) return kernel::KernelError::DeviceNotReady;// NOT READY (no medium)
      return kernel::KernelError::DeviceError;
    }
    if (result.uncertain) AtaRecoverChannel(ctx);
    return result.status;
  };

  if (const auto ready = AtaWaitReadStatus(io, true); ready.status) return fail(ready);
  for (size_t i = 0; i < 12; i += 2) {
    HAL_IO_Out16(AtaPort(io, kRegData), static_cast<uint16_t>(cdb[i] | (static_cast<uint16_t>(cdb[i + 1]) << 8)));
  }

  size_t received = 0;
  while (received < expected) {
    if (const auto ready = AtaWaitReadStatus(io, true); ready.status) return fail(ready);
    const size_t byteCount = HAL_IO_In8(AtaPort(io, kRegLbaMid)) |
                             (static_cast<size_t>(HAL_IO_In8(AtaPort(io, kRegLbaHigh))) << 8);
    if (!byteCount || byteCount % unit || byteCount > expected - received) {
      // Untrusted device byte count: never copy past the caller's buffer.
      KE_Print("ATAPI: invalid DRQ byte count {}", byteCount);
      AtaRecoverChannel(ctx);
      return kernel::KernelError::DeviceError;
    }
    for (size_t byte = 0; byte < byteCount; byte += 2) {
      const uint16_t word = HAL_IO_In16(AtaPort(io, kRegData));
      output[received + byte] = static_cast<uint8_t>(word);
      output[received + byte + 1] = static_cast<uint8_t>(word >> 8);
    }
    received += byteCount;
    transferred += byteCount;
  }

  if (const auto completed = AtaWaitReadStatus(io, false); completed.status) return fail(completed);
  return {};
}

kstd::error_code Atapi_ReadBatch(BusContext &ctx, const AtaContext &disk, uint32_t lba,
    uint16_t sectorCount, uint8_t *output, size_t &transferred) {
  const uint8_t cdb[12] = {
      0x28, 0,// READ(10)
      static_cast<uint8_t>(lba >> 24), static_cast<uint8_t>(lba >> 16),
      static_cast<uint8_t>(lba >> 8), static_cast<uint8_t>(lba),
      0,
      static_cast<uint8_t>(sectorCount >> 8), static_cast<uint8_t>(sectorCount),
      0, 0, 0};
  return Atapi_PacketRead(ctx, disk.isMaster, cdb, output,
      static_cast<size_t>(sectorCount) * kAtapiSectorSize, kAtapiSectorSize, transferred);
}

// READ CAPACITY(10). Returns the addressable 2048-byte sector count, or 0
// when there is no medium or the medium uses another block size.
uint32_t Atapi_ReadCapacity(BusContext &ctx, bool isMaster) {
  const uint8_t cdb[12] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t data[8]{};
  size_t transferred = 0;
  if (const auto status = Atapi_PacketRead(ctx, isMaster, cdb, data, sizeof(data), sizeof(data), transferred); status) {
    KE_Print("ATAPI: READ CAPACITY failed (no medium?)");
    return 0;
  }
  const uint32_t lastLba = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
                           (static_cast<uint32_t>(data[2]) << 8) | data[3];
  const uint32_t blockLength = (static_cast<uint32_t>(data[4]) << 24) | (static_cast<uint32_t>(data[5]) << 16) |
                               (static_cast<uint32_t>(data[6]) << 8) | data[7];
  if (blockLength != kAtapiSectorSize) {
    KE_Print("ATAPI: unsupported block length {}", blockLength);
    return 0;
  }
  return lastLba == 0xffffffffu ? lastLba : lastLba + 1;
}

kstd::error_code Ata_ProbeDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo,
                     const kernel::KDevicePtr<kernel::KDeviceBus> &fdo,
                     BusContext &ctx,
                     const bool isMaster) {
  const uint16_t ioBase = ctx.ioBase;
  const uint8_t driveSel = isMaster ? 0xA0 : 0xB0;

  // 1. Select device
  HAL_IO_Out8(AtaPort(ioBase, kRegDrive), driveSel);
  HAL_IO_Delay();

  // 400ns settle, then check for a floating bus (0xFF) or no device (0x00)
  // before issuing IDENTIFY or entering any BSY loop.
  uint8_t status = 0;
  for (int i = 0; i < 5; ++i) status = HAL_IO_In8(AtaPort(ioBase, kRegStatusCommand));
  if (status == 0x00 || status == 0xFF) {
    KE_Print("ATA: No device present");
    return kernel::KernelError::NotFound;
  }

  // 2. Clear registers
  HAL_IO_Out8(AtaPort(ioBase, kRegSectorCount), 0);
  HAL_IO_Out8(AtaPort(ioBase, kRegLbaLow), 0);
  HAL_IO_Out8(AtaPort(ioBase, kRegLbaMid), 0);
  HAL_IO_Out8(AtaPort(ioBase, kRegLbaHigh), 0);

  // 3. Issue IDENTIFY DEVICE
  HAL_IO_Out8(AtaPort(ioBase, kRegStatusCommand), 0xEC);
  HAL_IO_Delay();

  // A status of 0 right after IDENTIFY means no device (ATA/ATAPI-8).
  status = HAL_IO_In8(AtaPort(ioBase, kRegStatusCommand));
  if (status == 0x00) {
    KE_Print("ATA: No device present");
    return kernel::KernelError::NotFound;
  }

  // 4. Poll BSY until clear
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

  // 7. Wait for DRQ or ERR. ERR and DRQ are only valid once BSY is clear;
  // this keeps an ATAPI device's aborted IDENTIFY (ERR) from being mistaken
  // for a failure of the IDENTIFY PACKET that follows it.
  for (int i = 0; i < 100000; ++i) {
    status = HAL_IO_In8(AtaPort(ioBase, kRegStatusCommand));
    if (status & 0x80) {
      HAL_IO_Delay();
      continue;
    }
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

  KE_Print("ATA: Found {}: model='{}' serial='{}'", isAtapi ? "ATAPI device" : "disk", model, serial);

  // 9. ATAPI capacity comes from READ CAPACITY, not IDENTIFY.
  const uint32_t atapiSectors = isAtapi ? Atapi_ReadCapacity(ctx, isMaster) : 0;

  // 10. Create Disk PDO, named after the channel and drive position. The
  // serial is kept in AtaContext::serial.
  kstd::static_string<64> diskName;
  if (!pdo->Name().empty()) {
    kstd::format(diskName, "{}-{}", pdo->Name(), isMaster ? 0 : 1);
  } else {
    kstd::format(diskName, "ATA{x}-{}", ioBase, isMaster ? 0 : 1);
  }

  const auto created = KE_IO_CreateBusWithContext<AtaContext>(
      fdo->driverObject, diskName);
  if (!created) {
    KE_Print("ATA: Failed to create Disk PDO");
    return created.error();
  }
  const auto &disk = *created;
  disk->deviceType = isAtapi ? kernel::device_type::Optical : kernel::device_type::Disk;

  // 11. Fill deviceExtension
  const auto dctx = disk->deviceExtension.as<AtaContext>();
  dctx->pdo = pdo;
  dctx->model = model;
  dctx->serial = serial;
  dctx->isAtapi = isAtapi;
  dctx->isMaster = isMaster;
  disk->hardwareId = "disk";
  // LBA28 total addressable sectors live in words 60-61 of IDENTIFY data.
  dctx->sectorCount = isAtapi
                          ? atapiSectors
                          : (static_cast<uint32_t>(identify[60]) | (static_cast<uint32_t>(identify[61]) << 16));
  dctx->sectorSize = isAtapi ? kAtapiSectorSize : kAtaSectorSize;

  // 12. Report the Disk PDO to PnP
  return KE_PNP_ReportNewDevice(fdo, disk);
}


kstd::error_code Ata_Sector(uint16_t io, const AtaContext &disk, uint32_t lba,
    uint8_t (&sector)[kAtaSectorSize], bool write) {
  HAL_IO_Out8(AtaPort(io, kRegDrive), (disk.isMaster ? 0xe0 : 0xf0) | ((lba >> 24) & 15));
  // Let drive selection settle before issuing a command.
  for (int i = 0; i < 4; ++i) (void) HAL_IO_In8(AtaPort(io, kRegStatusCommand));
  if (const auto status = AtaWait(io, false, false); status) return status;
  HAL_IO_Out8(AtaPort(io, kRegFeatures), 0);
  HAL_IO_Out8(AtaPort(io, kRegSectorCount), 1);
  HAL_IO_Out8(AtaPort(io, kRegLbaLow), lba & 255);
  HAL_IO_Out8(AtaPort(io, kRegLbaMid), (lba >> 8) & 255);
  HAL_IO_Out8(AtaPort(io, kRegLbaHigh), (lba >> 16) & 255);
  HAL_IO_Out8(AtaPort(io, kRegStatusCommand), write ? 0x30 : 0x20);
  if (const auto status = AtaWait(io); status) return status;
  for (uint32_t i = 0; i < kAtaSectorSize; i += 2) {
    if (write) HAL_IO_Out16(AtaPort(io, kRegData),
        static_cast<uint16_t>(sector[i] | (static_cast<uint16_t>(sector[i + 1]) << 8)));
    else {
      const uint16_t word = HAL_IO_In16(AtaPort(io, kRegData));
      sector[i] = static_cast<uint8_t>(word);
      sector[i + 1] = static_cast<uint8_t>(word >> 8);
    }
  }
  return AtaWait(io, false);
}

kstd::error_code Ata_ReadBatch(BusContext &ctx, const AtaContext &disk, uint32_t lba,
    uint16_t sectorCount, uint8_t *output, size_t &transferred) {
  HAL_IO_Out8(AtaPort(ctx.ioBase, kRegDrive), (disk.isMaster ? 0xe0 : 0xf0) | ((lba >> 24) & 15));
  for (int i = 0; i < 4; ++i) (void) HAL_IO_In8(AtaPort(ctx.ioBase, kRegStatusCommand));
  if (const auto status = AtaWait(ctx.ioBase, false, false); status) return status;
  HAL_IO_Out8(AtaPort(ctx.ioBase, kRegFeatures), 0);
  HAL_IO_Out8(AtaPort(ctx.ioBase, kRegSectorCount), sectorCount == 256 ? 0 : static_cast<uint8_t>(sectorCount));
  HAL_IO_Out8(AtaPort(ctx.ioBase, kRegLbaLow), lba & 255);
  HAL_IO_Out8(AtaPort(ctx.ioBase, kRegLbaMid), (lba >> 8) & 255);
  HAL_IO_Out8(AtaPort(ctx.ioBase, kRegLbaHigh), (lba >> 16) & 255);
  HAL_IO_Out8(AtaPort(ctx.ioBase, kRegStatusCommand), 0x20);

  for (uint16_t sector = 0; sector < sectorCount; ++sector) {
    const auto ready = AtaWaitReadStatus(ctx.ioBase, true);
    if (ready.status) {
      if (ready.uncertain) AtaRecoverChannel(ctx);
      return ready.status;
    }

    for (uint32_t byte = 0; byte < kAtaSectorSize; byte += 2) {
      const uint16_t word = HAL_IO_In16(AtaPort(ctx.ioBase, kRegData));
      output[byte] = static_cast<uint8_t>(word);
      output[byte + 1] = static_cast<uint8_t>(word >> 8);
    }
    output += kAtaSectorSize;
    transferred += kAtaSectorSize;
  }

  const auto completed = AtaWaitReadStatus(ctx.ioBase, false);
  if (completed.status) {
    if (completed.uncertain) AtaRecoverChannel(ctx);
    return completed.status;
  }
  return {};
}

kstd::error_code Ata_Transfer(const kernel::KDevicePtr<kernel::KDevice> &device,
    uint64_t offset, uint8_t *readBuffer, const uint8_t *writeBuffer, size_t size, size_t &transferred) {
  transferred = 0;
  if (!device) return kernel::KernelError::Unsupported;
  const bool optical = device->deviceType == kernel::device_type::Optical;
  if (!optical && device->deviceType != kernel::device_type::Disk) return kernel::KernelError::Unsupported;
  const auto *disk = device->deviceExtension.as<AtaContext>();
  if (!disk || !disk->pdo || disk->isAtapi != optical) return kernel::KernelError::Unsupported;
  const size_t sectorSize = disk->sectorSize;
  if (sectorSize != (disk->isAtapi ? kAtapiSectorSize : kAtaSectorSize) || sectorSize > kMaxSectorSize)
    return kernel::KernelError::Unsupported;
  // ATAPI is read-only.
  if (disk->isAtapi && writeBuffer) return kernel::KernelError::Unsupported;
  // ATA stays within LBA28; ATAPI uses the READ CAPACITY sector count.
  const uint32_t capacitySectors = disk->isAtapi ? disk->sectorCount : kstd::min<uint32_t>(disk->sectorCount, 0x10000000u);
  const uint64_t capacity = static_cast<uint64_t>(capacitySectors) * sectorSize;
  if (offset > capacity || size > capacity - offset) return kernel::KernelError::OutOfRange;
  if (!size) return {};
  const auto bus = disk->pdo->attachedDevice.As<kernel::KDeviceBus>();
  if (!bus) return kernel::KernelError::DeviceError;
  auto *ctx = bus->deviceExtension.as<BusContext>();
  if (!ctx || !ctx->ioBase) return kernel::KernelError::DeviceNotReady;
  kstd::unique_lock lock(ctx->ioMutex);
  if (ctx->unusable) return kernel::KernelError::DeviceNotReady;

  auto readBatch = [&](uint32_t lba, uint16_t count, uint8_t *output, size_t &done) {
    return disk->isAtapi ? Atapi_ReadBatch(*ctx, *disk, lba, count, output, done)
                         : Ata_ReadBatch(*ctx, *disk, lba, count, output, done);
  };

  if (readBuffer) {
    size_t remaining = size;
    uint64_t position = offset;

    const size_t leading = static_cast<size_t>(position % sectorSize);
    if (leading) {
      uint8_t sector[kMaxSectorSize]{};
      size_t discarded = 0;
      if (const auto status = readBatch(
              static_cast<uint32_t>(position / sectorSize), 1, sector, discarded); status) return status;
      const size_t count = kstd::min(remaining, sectorSize - leading);
      for (size_t i = 0; i < count; ++i) readBuffer[transferred + i] = sector[leading + i];
      transferred += count;
      position += count;
      remaining -= count;
    }

    while (remaining >= sectorSize) {
      const uint32_t lba = static_cast<uint32_t>(position / sectorSize);
      const uint32_t throughCapacity = capacitySectors - lba;
      const uint32_t throughLimit = disk->isAtapi ? throughCapacity : 0x10000000u - lba;
      const uint32_t maxBatch = disk->isAtapi ? kAtapiMaxBatch : 256;
      const size_t sectorsRemaining = remaining / sectorSize;
      const uint16_t batch = static_cast<uint16_t>(kstd::min<size_t>(
          kstd::min<uint32_t>(maxBatch, kstd::min(throughCapacity, throughLimit)), sectorsRemaining));
      if (const auto status = readBatch(lba, batch, readBuffer + transferred, transferred); status) return status;
      const size_t bytes = static_cast<size_t>(batch) * sectorSize;
      position += bytes;
      remaining -= bytes;
    }

    if (remaining) {
      uint8_t sector[kMaxSectorSize]{};
      size_t discarded = 0;
      if (const auto status = readBatch(
              static_cast<uint32_t>(position / sectorSize), 1, sector, discarded); status) return status;
      for (size_t i = 0; i < remaining; ++i) readBuffer[transferred + i] = sector[i];
      transferred += remaining;
    }
    return {};
  }

  while (transferred < size) {
    const uint64_t position = offset + transferred;
    const uint32_t lba = static_cast<uint32_t>(position / kAtaSectorSize);
    const size_t begin = static_cast<size_t>(position % kAtaSectorSize);
    const size_t count = kstd::min(size - transferred, kAtaSectorSize - begin);
    uint8_t sector[kAtaSectorSize]{};
    if (writeBuffer) {
      if (begin || count != kAtaSectorSize) {
        size_t discarded = 0;
        if (const auto status = Ata_ReadBatch(*ctx, *disk, lba, 1, sector, discarded); status) return status;
      }
      for (size_t i = 0; i < count; ++i) sector[begin + i] = writeBuffer[transferred + i];
      if (const auto status = Ata_Sector(ctx->ioBase, *disk, lba, sector, true); status) return status;
      HAL_IO_Out8(AtaPort(ctx->ioBase, kRegStatusCommand), 0xe7); // FLUSH CACHE
      if (const auto status = AtaWait(ctx->ioBase, false); status) return status;
    }
    transferred += count;
  }
  return {};
}

kstd::error_code Ata_Read(const kernel::KDevicePtr<kernel::KDevice> &device,
    uint64_t offset, kstd::span<uint8_t> buffer, size_t &transferred) {
  return Ata_Transfer(device, offset, buffer.data(), nullptr, buffer.size(), transferred);
}

kstd::error_code Ata_Write(const kernel::KDevicePtr<kernel::KDevice> &device,
    uint64_t offset, kstd::span<const uint8_t> buffer, size_t &transferred) {
  return Ata_Transfer(device, offset, nullptr, buffer.data(), buffer.size(), transferred);
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

  // Resource contract (Docs/Storage.md, "ATA channel resources"): Port 0 is
  // the command block (>= 8 ports), Port 1 the control block (>= 1 port),
  // Interrupt 0 the IRQ (optional: I/O is polled).
  auto validPorts = [](const kernel::CmPartialResourceDescriptor *res, uint32_t minLength) {
    return res && res->length >= minLength && res->start <= 0xFFFFu &&
           res->length - 1 <= 0xFFFFu - res->start;
  };
  if (!validPorts(ioRes, kCommandBlockLength) || !validPorts(ctrlRes, kControlBlockLength)) {
    KE_Print("ATA: Command block (>= {} ports) or control block (>= {} port) missing or invalid; refusing channel",
             kCommandBlockLength, kControlBlockLength);
    ctx->unusable = true;
    return;
  }

  ctx->ioBase = static_cast<uint16_t>(ioRes->start);
  ctx->ctrlBase = static_cast<uint16_t>(ctrlRes->start);
  ctx->irq = irqRes ? irqRes->start : 0;

  KE_Print("ATA: Using I/O base 0x{x}, control 0x{x}, IRQ {}",
                  ctx->ioBase, ctx->ctrlBase, ctx->irq);

  if (!AtaSoftReset(*ctx)) {
    KE_Print("ATA: Soft reset could not be timed; channel unusable");
    return;
  }
  // Wait for BSY to clear. A floating or empty channel reads 0xFF/0x00 and is
  // reported by the probe.
  for (int i = 0; i < 4; ++i) (void) HAL_IO_In8(AtaPort(ctx->ioBase, kRegStatusCommand));
  for (int i = 0; i < 100000; ++i) {
    const uint8_t s = HAL_IO_In8(AtaPort(ctx->ioBase, kRegStatusCommand));
    if (!(s & 0x80) || s == 0xFF) break;
    HAL_IO_Delay();
  }

  KE_Print("ATA: Starting device identification...");
  // An absent drive is expected on most channels, so probe failures are not fatal.
  (void) Ata_ProbeDevice(pdo, fdo, *ctx, true); // master
  (void) Ata_ProbeDevice(pdo, fdo, *ctx, false);// slave
}

kstd::error_code Ata_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
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
