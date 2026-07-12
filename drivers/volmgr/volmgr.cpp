
#include <Kernel.hpp>
#include <array.hpp>
#include <new.hpp>

#include "volmgr_partition.hpp"

namespace {

constexpr size_t kMaxVolumes = 32;

// Extension of the volmgr filter FDO that is stacked on top of a Disk FDO. It
// remembers the disk it filters and the Volume PDOs it created (so the devnode
// dump can reach them).
struct VolMgrFdoContext {
  kernel::KDevicePtr<kernel::KDevice> disk;
  kstd::array<kernel::KDevicePtr<kernel::KDevice>, kMaxVolumes> volumes;
  size_t volumeCount;
};

// Extension of a Volume PDO: the disk to forward to plus this volume's window.
struct VolumeContext {
  kernel::KDevicePtr<kernel::KDevice> disk;
  uint64_t baseOffsetBytes;
  uint64_t lengthBytes;
};

// The driver object is captured at DriverEntry so StartDevice can create Volume
// PDOs owned by this driver.
kernel::KObjectPtr<kernel::KDriverObject> s_driver;

kernel::IoStatus VolMgr_Read(const kernel::KDevicePtr<kernel::KDevice> &device,
                             uint64_t offset,
                             kstd::span<uint8_t> buffer,
                             size_t &transferred) {
  transferred = 0;

  // Volume PDO check — correct
  if (!device || device->deviceType != kernel::device_type::Volume) {
    return kernel::IoStatus::Unsupported;
  }

  const auto *ctx = device->deviceExtension.as<VolumeContext>();
  if (!ctx || !ctx->disk) {
    return kernel::IoStatus::DeviceError;
  }

  uint64_t absOffset = 0;

  if (!volmgr::ComputeAbsoluteOffset(ctx->baseOffsetBytes,
                                     ctx->lengthBytes,
                                     offset,
                                     buffer.size(),
                                     absOffset)) {
    return kernel::IoStatus::OutOfRange;
  }

  return KE_IO_ReadDevice(ctx->disk, absOffset, buffer, transferred);
}

kernel::IoStatus VolMgr_Write(const kernel::KDevicePtr<kernel::KDevice> &device, uint64_t offset, kstd::span<const uint8_t> buffer, size_t &transferred) {
  transferred = 0;

  // Volume PDO check — correct
  if (!device || device->deviceType != kernel::device_type::Volume) {
    return kernel::IoStatus::Unsupported;
  }

  const auto *ctx = device->deviceExtension.as<VolumeContext>();
  if (!ctx || !ctx->disk) {
    return kernel::IoStatus::DeviceError;
  }

  uint64_t absOffset = 0;

  if (!volmgr::ComputeAbsoluteOffset(ctx->baseOffsetBytes,
                                     ctx->lengthBytes,
                                     offset,
                                     buffer.size(),
                                     absOffset)) {
    return kernel::IoStatus::OutOfRange;
  }

  return KE_IO_WriteDevice(ctx->disk, absOffset, buffer, transferred);
}

// Only the filter FDO enumerates children; Volume PDOs are leaves.

bool VolMgr_AddDevice(
    const kernel::KObjectPtr<kernel::KDriverObject> &driver,
    const kernel::KDevicePtr<kernel::KDevice> &diskFdo) {
  const auto filterFdo = KE_IO_CreateBusWithContext<VolMgrFdoContext>(driver);

  if (!filterFdo) return false;

  auto *ctx = filterFdo->deviceExtension.as<VolMgrFdoContext>();
  ctx->disk = diskFdo;

  filterFdo->lowerDevice = KE_IO_AttachFilterDevice(filterFdo, diskFdo);
  return true;
}

void VolMgr_StartDevice(const kernel::KDevicePtr<kernel::KDevice> &diskFdo) {
  if (diskFdo->deviceType != kernel::device_type::Disk) return;
  const auto filterFdo = diskFdo->attachedDevice.As<kernel::KDeviceBus>();// our filter, attached in AddDevice
  if (!filterFdo) return;
  auto *ctx = filterFdo->deviceExtension.as<VolMgrFdoContext>();
  if (!ctx) return;

  early_print("VolMgr: Disk attached, reading partition table...\r\n");

  kstd::array<uint8_t, volmgr::kSectorSize> sector0{};
  kstd::array<uint8_t, volmgr::kSectorSize> lba1{};
  size_t transferred = 0;

  if (const auto error = KE_IO_ReadDevice(diskFdo,
                                          0,
                                          kstd::span<uint8_t>(sector0),
                                          transferred);
      error != kernel::IoStatus::Success) {
    early_print_fmt("VolMgr: Failed to read sector 0: {}\r\n", static_cast<uint8_t>(error));
    return;
  }
  const bool haveLba1 = KE_IO_ReadDevice(diskFdo, volmgr::kSectorSize, kstd::span<uint8_t>(lba1), transferred) == kernel::IoStatus::Success;

  const volmgr::ByteView sector0View(sector0.data(), sector0.size());
  const volmgr::ByteView lba1View = haveLba1 ? volmgr::ByteView(lba1.data(), lba1.size()) : volmgr::ByteView{};

  const auto scheme = volmgr::DetectScheme(sector0View, lba1View);

  kstd::array<volmgr::PartitionInfo, kMaxVolumes> parts{};
  const kstd::span partsView(parts.data(), parts.size());
  size_t count = 0;

  if (scheme == volmgr::PartitionScheme::Mbr) {
    early_print("VolMgr: Detected MBR partition scheme\r\n");
    count = volmgr::ParseMbr(sector0View, partsView);
  } else if (scheme == volmgr::PartitionScheme::Gpt) {
    early_print("VolMgr: Detected GPT partition scheme\r\n");
    if (haveLba1) {
      const uint64_t entriesLba = volmgr::ReadLe64(lba1View.subspan(72));
      const uint32_t numEntries = volmgr::ReadLe32(lba1View.subspan(80));
      const uint32_t entrySize = volmgr::ReadLe32(lba1View.subspan(84));

      static kstd::array<uint8_t, 128 * 128> entriesBuf{};// bound to 128 entries * 128 bytes
      uint64_t bytesNeeded = static_cast<uint64_t>(numEntries) * entrySize;
      if (bytesNeeded > entriesBuf.size()) bytesNeeded = entriesBuf.size();

      if (bytesNeeded > 0 &&
          KE_IO_ReadDevice(diskFdo, entriesLba * volmgr::kSectorSize, kstd::span<uint8_t>(entriesBuf.data(), static_cast<size_t>(bytesNeeded)), transferred) == kernel::IoStatus::Success) {
        count = volmgr::ParseGptEntries(lba1View, volmgr::ByteView(entriesBuf.data(), static_cast<size_t>(bytesNeeded)), partsView);
      }
    }
  } else {
    early_print("VolMgr: No recognizable partition table\r\n");
    return;
  }

  for (size_t i = 0; i < count; i++) {
    // Name is <ParentName>Volume<i>
    kstd::static_string<64> nameBuf;
    kstd::format(nameBuf, "{}Volume{}", diskFdo->Name(), i);

    const auto volPdo = KE_IO_CreateDeviceWithContext<VolumeContext>(
        s_driver,
        nameBuf,
        kernel::device_type::Volume);
    if (!volPdo) continue;

    auto *vctx = volPdo->deviceExtension.as<VolumeContext>();
    vctx->disk = diskFdo;
    vctx->baseOffsetBytes = parts[i].baseOffsetBytes;
    vctx->lengthBytes = parts[i].lengthBytes;

    if (ctx->volumeCount < kMaxVolumes) {
      ctx->volumes[ctx->volumeCount++] = volPdo;
    }

    early_print_fmt("VolMgr: Volume {} uses disk '{}', baseLBA={}, sectors={}\r\n",
                    i,
                    diskFdo->Name(),
                    parts[i].baseOffsetBytes / volmgr::kSectorSize,
                    parts[i].lengthBytes / volmgr::kSectorSize);

    // Report the Volume PDO to PnP so a filesystem driver could bind later.
    KE_PNP_ReportNewDevice(filterFdo, volPdo);
  }
}

}// namespace

UNDOS_DRIVER_ENTRY {
  s_driver = driver;

  driver->AddDevice = VolMgr_AddDevice;
  driver->StartDevice = VolMgr_StartDevice;
  driver->Read = VolMgr_Read;
  driver->Write = VolMgr_Write;

  // Trigger: attach to any Disk that appears in the PnP tree.
  KE_PNP_RegisterDeviceInterest(kernel::device_type::Disk, driver);
}
