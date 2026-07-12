
#include <Kernel.hpp>
#include <array.hpp>
#include <new.hpp>

namespace {
kernel::KObjectPtr<kernel::KDriverObject> g_driver = nullptr;

struct [[gnu::packed]] FAT_BPB_Common {
  uint8_t jmpBoot[3];
  char oemName[8];
  uint16_t bytesPerSector;
  uint8_t sectorsPerCluster;
  uint16_t reservedSectors;
  uint8_t numFATs;
  uint16_t rootEntryCount;
  uint16_t totalSectors16;
  uint8_t media;
  uint16_t fatSize16;
  uint16_t sectorsPerTrack;
  uint16_t numHeads;
  uint32_t hiddenSectors;
  uint32_t totalSectors32;
};

struct [[gnu::packed]] FAT_BPB_FAT32 {
  uint32_t fatSize32;
  uint16_t extFlags;
  uint16_t fsVersion;
  uint32_t rootCluster;
  uint16_t fsInfo;
  uint16_t backupBootSector;
  uint8_t reserved[12];
};

struct [[gnu::packed]] EXFAT_BPB {
  uint8_t jumpBoot[3];
  uint8_t fsName[8];// "EXFAT   "
  uint8_t mustBeZero[53];
  uint64_t partitionOffset;
  uint64_t volumeLength;
  uint32_t fatOffset;
  uint32_t fatLength;
  uint32_t clusterHeapOffset;
  uint32_t clusterCount;
  uint32_t rootDirCluster;
  uint32_t volumeSerial;
  uint8_t fsRevision[2];
  uint8_t volumeFlags[2];
  uint8_t bytesPerSectorShift;
  uint8_t sectorsPerClusterShift;
  uint8_t numFats;
  uint8_t driveSelect;
  uint8_t percentInUse;
  uint8_t reserved2[7];
};

enum class FAT_Type { FAT12,
                      FAT16,
                      FAT32,
                      ExFAT };

struct FAT_Volume {
  FAT_Type type;
  uint32_t bytesPerSector;
  uint32_t sectorsPerCluster;
  uint32_t reservedSectors;
  uint32_t numFATs;
  uint32_t fatSizeSectors;
  uint32_t firstFATSector;
  uint32_t rootDirSector;// FAT12/16
  uint32_t rootCluster;  // FAT32/ExFAT
  uint32_t dataStartSector;
  uint32_t totalClusters;
  // maybe: pointers to on‑disk FSInfo, etc.
};

bool FAT_MountVolume(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const kstd::string_view &options) {
  (void) options;

  uint8_t sector[512];
  size_t buffer_actual_transfer = 0;

  if (KE_IO_ReadDevice(mountPoint->volume, 0, sector, buffer_actual_transfer) != kernel::IoStatus::Success) {
    early_print_fmt("FS_FAT: Failed to read BPB\r\n");
    return false;
  }

  if (buffer_actual_transfer < sizeof(FAT_BPB_Common)) {
    early_print_fmt("FS_FAT: Failed to read BPB\r\n");
    return false;
  }

  const auto *bpb = reinterpret_cast<FAT_BPB_Common *>(sector);

  {
    early_print_fmt("FS_FAT: BPB Dump:\r\n");
    early_print_fmt("  jmpBoot: 0x{:x} 0x{:x} 0x{:x}\r\n", bpb->jmpBoot[0], bpb->jmpBoot[1], bpb->jmpBoot[2]);
    early_print_fmt("  oemName: {}\r\n", kstd::string_view(bpb->oemName, 8));
    early_print_fmt("  bytesPerSector: {}\r\n", bpb->bytesPerSector);
    early_print_fmt("  sectorsPerCluster: {}\r\n", bpb->sectorsPerCluster);
    early_print_fmt("  reservedSectors: {}\r\n", bpb->reservedSectors);
    early_print_fmt("  numFATs: {}\r\n", bpb->numFATs);
    early_print_fmt("  rootEntryCount: {}\r\n", bpb->rootEntryCount);
    early_print_fmt("  totalSectors16: {}\r\n", bpb->totalSectors16);
    early_print_fmt("  media: 0x{:x}\r\n", bpb->media);
    early_print_fmt("  fatSize16: {}\r\n", bpb->fatSize16);
    early_print_fmt("  sectorsPerTrack: {}\r\n", bpb->sectorsPerTrack);
    early_print_fmt("  numHeads: {}\r\n", bpb->numHeads);
    early_print_fmt("  hiddenSectors: {}\r\n", bpb->hiddenSectors);
    early_print_fmt("  totalSectors32: {}\r\n", bpb->totalSectors32);
  }

  if (bpb->bytesPerSector == 0 || bpb->sectorsPerCluster == 0) {
    early_print_fmt("FS_FAT: Invalid BPB\r\n");
    return false;
  }

  // Check for ExFAT by reading full EXFAT_BPB and fsName
  {
    EXFAT_BPB exfat{};
    size_t exfat_transfer = 0;
    kstd::span exfatBuf(reinterpret_cast<uint8_t *>(&exfat), sizeof(EXFAT_BPB));

    if (KE_IO_ReadDevice(mountPoint->volume, 0, exfatBuf, exfat_transfer) == kernel::IoStatus::Success &&
        exfat_transfer >= sizeof(EXFAT_BPB)) {
      kstd::string_view fsName(reinterpret_cast<const char *>(exfat.fsName), 8);
      if (fsName == "EXFAT   ") {
        early_print_fmt("FS_FAT: ExFAT Detected\r\n");
        auto vol = kernel::DataBuffer::Create<FAT_Volume>();
        vol.as<FAT_Volume>()->type = FAT_Type::ExFAT;
        vol.as<FAT_Volume>()->bytesPerSector = 1u << exfat.bytesPerSectorShift;
        vol.as<FAT_Volume>()->sectorsPerCluster = 1u << exfat.sectorsPerClusterShift;
        vol.as<FAT_Volume>()->numFATs = exfat.numFats;
        vol.as<FAT_Volume>()->fatSizeSectors = exfat.fatLength;
        vol.as<FAT_Volume>()->firstFATSector = exfat.fatOffset;
        vol.as<FAT_Volume>()->dataStartSector = exfat.clusterHeapOffset;
        vol.as<FAT_Volume>()->rootCluster = exfat.rootDirCluster;
        vol.as<FAT_Volume>()->totalClusters = exfat.clusterCount;
        mountPoint->driverExtension = kstd::move(vol);
        return true;
      }
    }
  }

  // Classic FAT (12/16/32)
  const auto *fat32 = reinterpret_cast<FAT_BPB_FAT32 *>(sector + sizeof(FAT_BPB_Common));

  const uint32_t totalSectors = bpb->totalSectors16 ? bpb->totalSectors16 : bpb->totalSectors32;
  const uint32_t fatSize = bpb->fatSize16 ? bpb->fatSize16 : fat32->fatSize32;
  const uint32_t rootDirSectors = (static_cast<uint32_t>(bpb->rootEntryCount) * 32u + (static_cast<uint32_t>(bpb->bytesPerSector) - 1u)) / static_cast<uint32_t>(bpb->bytesPerSector);
  const uint32_t dataSectors = totalSectors - bpb->reservedSectors - (bpb->numFATs * fatSize) - rootDirSectors;
  const uint32_t totalClusters = dataSectors / bpb->sectorsPerCluster;

  auto vol = kernel::DataBuffer::Create<FAT_Volume>();

  vol.as<FAT_Volume>()->bytesPerSector = bpb->bytesPerSector;
  vol.as<FAT_Volume>()->sectorsPerCluster = bpb->sectorsPerCluster;
  vol.as<FAT_Volume>()->reservedSectors = bpb->reservedSectors;
  vol.as<FAT_Volume>()->numFATs = bpb->numFATs;
  vol.as<FAT_Volume>()->fatSizeSectors = fatSize;
  vol.as<FAT_Volume>()->firstFATSector = bpb->reservedSectors;
  vol.as<FAT_Volume>()->totalClusters = totalClusters;

  if (totalClusters < 4085) {
    vol.as<FAT_Volume>()->type = FAT_Type::FAT12;
    vol.as<FAT_Volume>()->rootDirSector = bpb->reservedSectors + bpb->numFATs * fatSize;
    vol.as<FAT_Volume>()->dataStartSector = vol.as<FAT_Volume>()->rootDirSector + rootDirSectors;
    early_print_fmt("FS_FAT: FAT12 Detected\r\n");
  } else if (totalClusters < 65525) {
    vol.as<FAT_Volume>()->type = FAT_Type::FAT16;
    vol.as<FAT_Volume>()->rootDirSector = bpb->reservedSectors + bpb->numFATs * fatSize;
    vol.as<FAT_Volume>()->dataStartSector = vol.as<FAT_Volume>()->rootDirSector + rootDirSectors;
    early_print_fmt("FS_FAT: FAT16 Detected\r\n");
  } else {
    vol.as<FAT_Volume>()->type = FAT_Type::FAT32;
    vol.as<FAT_Volume>()->rootCluster = fat32->rootCluster;
    vol.as<FAT_Volume>()->dataStartSector = bpb->reservedSectors + bpb->numFATs * fatSize;
    early_print_fmt("FS_FAT: FAT32 Detected\r\n");
  }

  mountPoint->driverExtension = kstd::move(vol);
  return true;
}

}// namespace

UNDOS_DRIVER_ENTRY {
  g_driver = driver;

  if (const auto fs = KE_VFS_RegisterFilesystemDriver(g_driver, "FAT")) {
    fs->MountVolume = FAT_MountVolume;
  }


  // Don't register interest, the VFS subsystem will take care of this
}
