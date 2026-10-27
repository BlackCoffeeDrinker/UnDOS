
#include <Kernel.hpp>
#include <array.hpp>
#include <new.hpp>

#include "fat_short_name.hpp"

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
  uint32_t rootDirSector; // FAT12/16
  uint32_t rootDirSectors;// FAT12/16 (size, in sectors, of the fixed root dir area)
  uint32_t rootCluster;   // FAT32/ExFAT
  uint32_t dataStartSector;
  uint32_t totalClusters;
  bool writable{true};
  uint64_t traversalGeneration{1};
  bool traversalReuseDisabled{false};
  uint32_t fsInfoSector{0};
  uint32_t backupFsInfoSector{0};
  mutable bool fsInfoInvalidated{false};
  // maybe: pointers to on‑disk FSInfo, etc.
};

bool FAT_IsPowerOfTwo(uint32_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

bool FAT_HasVbrSignature(const uint8_t *sector) {
  return sector[510] == 0x55 && sector[511] == 0xAA;
}

kstd::error_code FAT_ReadVbr(const kernel::KDevicePtr<kernel::KDevice> &device,
    uint8_t (&sector)[512]) {
  size_t transferred = 0;
  if (const auto status = KE_IO_ReadDevice(device, 0, kstd::span(sector), transferred); status)
    return status;
  if (transferred != sizeof(sector))
    return kernel::make_error_code(kernel::KernelError::UnexpectedEndOfFile);
  return {};
}

// Mounting and volume-ID discovery use this parser so both paths apply the
// same VBR signature, BPB, cluster-count, and FAT-capacity checks.
kstd::expected<FAT_Type, kstd::error_code> FAT_ParseVbr(
    const uint8_t *sector, FAT_Volume &vol, bool &hasSerial, uint32_t &serial) {
  hasSerial = false;
  serial = 0;
  if (!FAT_HasVbrSignature(sector))
    return kstd::unexpected(kernel::make_error_code(kernel::KernelError::InvalidFormat));

  const auto *exfat = reinterpret_cast<const EXFAT_BPB *>(sector);
  if (kstd::string_view(reinterpret_cast<const char *>(exfat->fsName), 8) == "EXFAT   ") {
    if (exfat->bytesPerSectorShift < 9 || exfat->bytesPerSectorShift > 12 ||
        exfat->sectorsPerClusterShift > 7)
      return kstd::unexpected(kernel::make_error_code(kernel::KernelError::InvalidFormat));
    vol.type = FAT_Type::ExFAT;
    vol.bytesPerSector = 1u << exfat->bytesPerSectorShift;
    vol.sectorsPerCluster = 1u << exfat->sectorsPerClusterShift;
    vol.numFATs = exfat->numFats;
    vol.fatSizeSectors = exfat->fatLength;
    vol.firstFATSector = exfat->fatOffset;
    vol.dataStartSector = exfat->clusterHeapOffset;
    vol.rootCluster = exfat->rootDirCluster;
    vol.totalClusters = exfat->clusterCount;
    return FAT_Type::ExFAT;
  }

  const auto *bpb = reinterpret_cast<const FAT_BPB_Common *>(sector);
  const uint32_t totalSectors = bpb->totalSectors16 ? bpb->totalSectors16 : bpb->totalSectors32;
  if (bpb->bytesPerSector < 512 || bpb->bytesPerSector > 4096 ||
      !FAT_IsPowerOfTwo(bpb->bytesPerSector) ||
      !FAT_IsPowerOfTwo(bpb->sectorsPerCluster) || bpb->sectorsPerCluster > 128 ||
      !bpb->reservedSectors || !bpb->numFATs || !totalSectors)
    return kstd::unexpected(kernel::make_error_code(kernel::KernelError::InvalidFormat));

  const auto *fat32 = reinterpret_cast<const FAT_BPB_FAT32 *>(sector + sizeof(FAT_BPB_Common));
  const uint32_t fatSize = bpb->fatSize16 ? bpb->fatSize16 : fat32->fatSize32;
  if (!fatSize ||
      static_cast<uint64_t>(bpb->reservedSectors) +
              static_cast<uint64_t>(bpb->numFATs) * fatSize >= totalSectors)
    return kstd::unexpected(kernel::make_error_code(kernel::KernelError::InvalidFormat));

  const uint32_t rootDirSectors =
      (static_cast<uint32_t>(bpb->rootEntryCount) * 32u + bpb->bytesPerSector - 1u) /
      bpb->bytesPerSector;
  const uint64_t metadataSectors = static_cast<uint64_t>(bpb->reservedSectors) +
      static_cast<uint64_t>(bpb->numFATs) * fatSize + rootDirSectors;
  if (metadataSectors >= totalSectors)
    return kstd::unexpected(kernel::make_error_code(kernel::KernelError::InvalidFormat));

  const uint32_t totalClusters =
      (totalSectors - static_cast<uint32_t>(metadataSectors)) / bpb->sectorsPerCluster;
  if (totalClusters >= 0x0ffffff0u)
    return kstd::unexpected(kernel::make_error_code(kernel::KernelError::InvalidFormat));

  vol.bytesPerSector = bpb->bytesPerSector;
  vol.sectorsPerCluster = bpb->sectorsPerCluster;
  vol.reservedSectors = bpb->reservedSectors;
  vol.numFATs = bpb->numFATs;
  vol.fatSizeSectors = fatSize;
  vol.firstFATSector = bpb->reservedSectors;
  vol.totalClusters = totalClusters;

  if (totalClusters < 4085) {
    vol.type = FAT_Type::FAT12;
    vol.rootDirSector = bpb->reservedSectors + bpb->numFATs * fatSize;
    vol.rootDirSectors = rootDirSectors;
    vol.dataStartSector = vol.rootDirSector + rootDirSectors;
  } else if (totalClusters < 65525) {
    vol.type = FAT_Type::FAT16;
    vol.rootDirSector = bpb->reservedSectors + bpb->numFATs * fatSize;
    vol.rootDirSectors = rootDirSectors;
    vol.dataStartSector = vol.rootDirSector + rootDirSectors;
  } else {
    vol.type = FAT_Type::FAT32;
    vol.dataStartSector = bpb->reservedSectors + bpb->numFATs * fatSize;
  }

  const uint64_t requiredFatBytes = vol.type == FAT_Type::FAT12
      ? (static_cast<uint64_t>(totalClusters + 2) * 3 + 1) / 2
      : static_cast<uint64_t>(totalClusters + 2) *
            (vol.type == FAT_Type::FAT16 ? 2 : 4);
  if (requiredFatBytes > static_cast<uint64_t>(fatSize) * vol.bytesPerSector)
    return kstd::unexpected(kernel::make_error_code(kernel::KernelError::InvalidFormat));

  if (vol.type == FAT_Type::FAT32) {
    vol.rootCluster = fat32->rootCluster;
    if (vol.rootCluster < 2 || vol.rootCluster >= totalClusters + 2)
      return kstd::unexpected(kernel::make_error_code(kernel::KernelError::InvalidFormat));
    vol.writable = !(fat32->extFlags & 0x80) && fat32->fsVersion == 0;
    if (fat32->fsInfo > 0 && fat32->fsInfo < bpb->reservedSectors) {
      vol.fsInfoSector = fat32->fsInfo;
      const uint32_t backup = static_cast<uint32_t>(fat32->backupBootSector) + fat32->fsInfo;
      if (fat32->backupBootSector && backup < bpb->reservedSectors)
        vol.backupFsInfoSector = backup;
    }
    if (sector[66] == 0x28 || sector[66] == 0x29) {
      serial = static_cast<uint32_t>(sector[67]) |
          (static_cast<uint32_t>(sector[68]) << 8) |
          (static_cast<uint32_t>(sector[69]) << 16) |
          (static_cast<uint32_t>(sector[70]) << 24);
      hasSerial = true;
    }
  } else if (sector[38] == 0x28 || sector[38] == 0x29) {
    serial = static_cast<uint32_t>(sector[39]) |
        (static_cast<uint32_t>(sector[40]) << 8) |
        (static_cast<uint32_t>(sector[41]) << 16) |
        (static_cast<uint32_t>(sector[42]) << 24);
    hasSerial = true;
  }

  return vol.type;
}

constexpr uint32_t kFatFirstDataCluster = 2;

// Short (8.3) directory entry, 32 bytes.
struct [[gnu::packed]] FAT_DirEntry {
  char name[11];
  uint8_t attr;
  uint8_t ntReserved;
  uint8_t createTimeTenth;
  uint16_t createTime;
  uint16_t createDate;
  uint16_t lastAccessDate;
  uint16_t firstClusterHigh;
  uint16_t writeTime;
  uint16_t writeDate;
  uint16_t firstClusterLow;
  uint32_t fileSize;
};

enum FAT_Attr : uint8_t {
  kAttrReadOnly = 0x01,
  kAttrHidden = 0x02,
  kAttrSystem = 0x04,
  kAttrVolumeId = 0x08,
  kAttrDirectory = 0x10,
  kAttrArchive = 0x20,
  kAttrLongName = kAttrReadOnly | kAttrHidden | kAttrSystem | kAttrVolumeId,
};

kstd::error_code FAT_MountVolume(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const kstd::string_view &options) {
  (void) options;

  uint8_t sector[512];
  if (const auto status = FAT_ReadVbr(mountPoint->volume, sector); status) {
    KE_Print("FS_FAT: Failed to read BPB");
    return status;
  }

  const auto *bpb = reinterpret_cast<FAT_BPB_Common *>(sector);

  KE_Print("FS_FAT: BPB Dump:");
  KE_Print("  jmpBoot: 0x{:x} 0x{:x} 0x{:x}", bpb->jmpBoot[0], bpb->jmpBoot[1], bpb->jmpBoot[2]);
  KE_Print("  oemName: {}", kstd::string_view(bpb->oemName, 8));
  KE_Print("  bytesPerSector: {}", bpb->bytesPerSector);
  KE_Print("  sectorsPerCluster: {}", bpb->sectorsPerCluster);
  KE_Print("  reservedSectors: {}", bpb->reservedSectors);
  KE_Print("  numFATs: {}", bpb->numFATs);
  KE_Print("  rootEntryCount: {}", bpb->rootEntryCount);
  KE_Print("  totalSectors16: {}", bpb->totalSectors16);
  KE_Print("  media: 0x{:x}", bpb->media);
  KE_Print("  fatSize16: {}", bpb->fatSize16);
  KE_Print("  sectorsPerTrack: {}", bpb->sectorsPerTrack);
  KE_Print("  numHeads: {}", bpb->numHeads);
  KE_Print("  hiddenSectors: {}", bpb->hiddenSectors);
  KE_Print("  totalSectors32: {}", bpb->totalSectors32);

  FAT_Volume parsedVolume{};
  bool hasSerial = false;
  uint32_t serial = 0;
  const auto parsed = FAT_ParseVbr(sector, parsedVolume, hasSerial, serial);
  if (!parsed) return parsed.error();

  mountPoint->driverExtension = kernel::DataBuffer::Create<FAT_Volume>();
  if (!mountPoint->driverExtension) return kernel::make_error_code(kernel::KernelError::OutOfMemory);
  const auto vol = mountPoint->driverExtension.as<FAT_Volume>();
  *vol = parsedVolume;

  switch (*parsed) {
    case FAT_Type::FAT12: KE_Print("FS_FAT: FAT12 Detected"); break;
    case FAT_Type::FAT16: KE_Print("FS_FAT: FAT16 Detected"); break;
    case FAT_Type::FAT32: KE_Print("FS_FAT: FAT32 Detected"); break;
    case FAT_Type::ExFAT: KE_Print("FS_FAT: ExFAT Detected"); break;
  }

  return {};
}

kstd::expected<bool, kstd::error_code> FAT_ProbeVolumeId(
    const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t &serial) {
  uint8_t sector[512];
  if (const auto status = FAT_ReadVbr(device, sector); status)
    return kstd::unexpected(status);

  FAT_Volume volume{};
  bool hasSerial = false;
  uint32_t parsedSerial = 0;
  const auto parsed = FAT_ParseVbr(sector, volume, hasSerial, parsedSerial);
  if (!parsed || *parsed == FAT_Type::ExFAT || !hasSerial) return false;

  serial = parsedSerial;
  return true;
}

struct FAT_ChainCheckpoint {
  bool valid{false};
  uint64_t generation{0};
  uint32_t startCluster{0};
  uint64_t clusterIndex{0};
  uint32_t cluster{0};
};

struct FAT_OrdinalCheckpoint {
  bool valid{false};
  uint64_t generation{0};
  uint64_t ordinal{0};
  uint32_t sector{0};
  uint32_t slot{0};
  bool pendingLongName{false};
};

struct FAT_VFSNode {
  uint32_t startCluster = 0;// 0 => fixed-size FAT12/16 root directory area
  bool isFixedRoot = false;
  FAT_ChainCheckpoint fileChain{};
  FAT_ChainCheckpoint directoryChain{};
  FAT_OrdinalCheckpoint directoryOrdinal{};
  bool directoryEofKnown{false};
  uint64_t directoryEofCount{0};
  uint64_t directoryEofGeneration{0};
};

constexpr uint32_t kFatEndOfChainThreshold12 = 0x0FF8;
constexpr uint32_t kFatEndOfChainThreshold16 = 0xFFF8;
constexpr uint32_t kFatEndOfChainThreshold32 = 0x0FFFFFF8;

bool FAT_IsEndOfChain(const FAT_Volume *vol, uint32_t cluster) {
  switch (vol->type) {
    case FAT_Type::FAT12: return cluster >= kFatEndOfChainThreshold12;
    case FAT_Type::FAT16: return cluster >= kFatEndOfChainThreshold16;
    case FAT_Type::FAT32:
    case FAT_Type::ExFAT: return cluster >= kFatEndOfChainThreshold32;
  }
  return true;
}

bool FAT_ValidCluster(const FAT_Volume &vol, uint32_t cluster) {
  return cluster >= 2 && cluster - 2 < vol.totalClusters;
}

uint32_t FAT_ClusterToSector(const FAT_Volume *vol, uint32_t cluster) {
  return vol->dataStartSector + (cluster - kFatFirstDataCluster) * vol->sectorsPerCluster;
}

// Reads the FAT table entry for `cluster` and returns the next cluster in the chain
// (or an end-of-chain marker, see FAT_IsEndOfChain). Device failures are reported as errors.
kstd::expected<uint32_t, kstd::error_code> FAT_GetNextCluster(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const FAT_Volume *vol, uint32_t cluster) {
  uint32_t byteOffset;
  switch (vol->type) {
    case FAT_Type::FAT12: byteOffset = cluster + (cluster / 2); break;
    case FAT_Type::FAT16: byteOffset = cluster * 2; break;
    default: byteOffset = cluster * 4; break;
  }

  const uint32_t fatSector = vol->firstFATSector + (byteOffset / vol->bytesPerSector);
  const uint32_t entryOffset = byteOffset % vol->bytesPerSector;

  // Read two consecutive sectors in case a FAT12 entry straddles a sector boundary.
  const auto buf = kernel::DataBuffer::Alloc(static_cast<size_t>(vol->bytesPerSector) * 2);
  if (!buf) return kstd::unexpected(kernel::make_error_code(kernel::KernelError::OutOfMemory));
  size_t transferred = 0;
  if (const auto status = KE_IO_ReadDevice(mountPoint->volume, static_cast<uint64_t>(fatSector) * vol->bytesPerSector,
                                           kstd::span(buf.as<uint8_t>(), buf.size), transferred);
      status.value() != 0) {
    return kstd::unexpected(status);
  }
  if (transferred < entryOffset + (vol->type == FAT_Type::FAT12 || vol->type == FAT_Type::FAT16 ? 2u : 4u)) {
    return kstd::unexpected(kernel::make_error_code(kernel::KernelError::UnexpectedEndOfFile));
  }

  const uint8_t *bytes = buf.as<uint8_t>();
  uint32_t value;
  switch (vol->type) {
    case FAT_Type::FAT12: {
      const uint32_t raw = bytes[entryOffset] | (static_cast<uint32_t>(bytes[entryOffset + 1]) << 8);
      value = (cluster & 1u) ? (raw >> 4) : (raw & 0x0FFFu);
      break;
    }
    case FAT_Type::FAT16: {
      value = bytes[entryOffset] | (static_cast<uint32_t>(bytes[entryOffset + 1]) << 8);
      break;
    }
    default: {
      value = bytes[entryOffset] | (static_cast<uint32_t>(bytes[entryOffset + 1]) << 8) |
              (static_cast<uint32_t>(bytes[entryOffset + 2]) << 16) | (static_cast<uint32_t>(bytes[entryOffset + 3]) << 24);
      value &= 0x0FFFFFFFu;
      break;
    }
  }

  return value;
}

// Reads a single sector belonging to a directory (either the FAT12/16 fixed root area, or
// a regular cluster chain). Returns false once there is nothing left to read and an
// error if the device or FAT chain could not be read.
kstd::expected<bool, kstd::error_code> FAT_ReadDirSector(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const FAT_Volume *vol,
                                                            const FAT_VFSNode *dir, uint32_t &cluster, uint32_t &sectorInCluster, uint32_t &fixedSectorIndex,
                                                            const kstd::span<uint8_t> &buffer) {
  uint32_t sector;

  if (dir->isFixedRoot) {
    if (fixedSectorIndex >= vol->rootDirSectors) return false;
    sector = vol->rootDirSector + fixedSectorIndex;
    fixedSectorIndex++;
  } else {
    if (cluster == 0 || FAT_IsEndOfChain(vol, cluster)) return false;
    sector = FAT_ClusterToSector(vol, cluster) + sectorInCluster;
    sectorInCluster++;
    if (sectorInCluster >= vol->sectorsPerCluster) {
      sectorInCluster = 0;
      const auto next = FAT_GetNextCluster(mountPoint, vol, cluster);
      if (!next) return kstd::unexpected(next.error());
      cluster = *next;
    }
  }

  size_t transferred = 0;
  if (const auto status = KE_IO_ReadDevice(mountPoint->volume, static_cast<uint64_t>(sector) * vol->bytesPerSector, buffer, transferred);
      status) {
    return kstd::unexpected(status);
  }
  if (transferred != buffer.size()) return kstd::unexpected(kernel::KernelError::UnexpectedEndOfFile);
  return true;
}

kstd::error_code FAT_FillNode(const FAT_DirEntry &entry, kernel::KVFSNode &out) {
  const uint32_t startCluster = (static_cast<uint32_t>(entry.firstClusterHigh) << 16) | entry.firstClusterLow;

  out.type = (entry.attr & kAttrDirectory) ? kernel::VFSNodeType::Directory : kernel::VFSNodeType::File;
  out.size = (entry.attr & kAttrDirectory) ? 0 : entry.fileSize;
  out.attributes = entry.attr;
  out.fsPrivate = kernel::DataBuffer::Create<FAT_VFSNode>();
  if (!out.fsPrivate) return kernel::KernelError::OutOfMemory;
  auto *node = out.fsPrivate.as<FAT_VFSNode>();
  node->startCluster = startCluster;
  node->isFixedRoot = false;
  return kernel::KernelError::Success;
}

kstd::error_code FAT_CreateHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const kernel::KVFSNode &vnode, kernel::KIOObject::OpenMode mode) {
  (void) mountPoint;
  if ((mode & kernel::KIOObject::OpenMode::Create) && (mode & kernel::KIOObject::OpenMode::Exclusive))
    return kernel::KernelError::AlreadyExists;
  if (mode & (kernel::KIOObject::OpenMode::Truncate | kernel::KIOObject::OpenMode::Append))
    return kernel::KernelError::Unsupported;
  if (vnode.type != kernel::VFSNodeType::File && vnode.type != kernel::VFSNodeType::Directory)
    return kernel::KernelError::InvalidState;

  return kernel::KernelError::Success;
}

kstd::expected<uint64_t, kstd::error_code> FAT_ReadHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const kernel::KVFSNode &node, uint64_t offset, const kstd::span<uint8_t> &destination) {
  uint64_t actually_read = 0;

  if (node.type != kernel::VFSNodeType::File) return kstd::unexpected(kernel::KernelError::InvalidState);
  if (offset >= node.size || destination.empty()) return 0ull;
  const uint64_t read_size = destination.size() > node.size - offset ? node.size - offset : destination.size();

  const auto *vol = mountPoint->driverExtension.as<FAT_Volume>();
  auto *fatNode = node.fsPrivate.as<FAT_VFSNode>();
  const uint32_t clusterSize = vol->bytesPerSector * vol->sectorsPerCluster;

  if (fatNode->startCluster == 0 || clusterSize == 0) return kstd::unexpected(kernel::KernelError::InvalidFormat);

  const uint64_t clusterIndex = offset / clusterSize;
  const uint64_t offsetInFirstCluster = offset % clusterSize;
  uint32_t cluster = fatNode->startCluster;
  uint64_t currentIndex = 0;
  const bool checkpointCurrent = !vol->traversalReuseDisabled && fatNode->fileChain.valid &&
      fatNode->fileChain.generation == vol->traversalGeneration &&
      fatNode->fileChain.startCluster == fatNode->startCluster &&
      FAT_ValidCluster(*vol, fatNode->fileChain.cluster);
  const bool reusable = checkpointCurrent && fatNode->fileChain.clusterIndex <= clusterIndex;
  if (reusable) {
    cluster = fatNode->fileChain.cluster;
    currentIndex = fatNode->fileChain.clusterIndex;
  } else if (!checkpointCurrent) {
    fatNode->fileChain.valid = false;
  }

  while (currentIndex < clusterIndex) {
    const auto next = FAT_GetNextCluster(mountPoint, vol, cluster);
    if (!next) return kstd::unexpected(next.error());
    cluster = *next;
    if (FAT_IsEndOfChain(vol, cluster)) return kstd::unexpected(kernel::KernelError::UnexpectedEndOfFile);
    ++currentIndex;
  }

  const auto clusterBuf = kernel::DataBuffer::Alloc(clusterSize);
  if (!clusterBuf) return kstd::unexpected(kernel::KernelError::OutOfMemory);
  uint64_t offsetInCluster = offsetInFirstCluster;
  uint32_t lastReadCluster = cluster;
  uint64_t lastReadIndex = currentIndex;

  while (actually_read < read_size) {
    if (FAT_IsEndOfChain(vol, cluster)) break;

    size_t transferred = 0;
    const uint32_t sector = FAT_ClusterToSector(vol, cluster);
    if (const auto status = KE_IO_ReadDevice(mountPoint->volume, static_cast<uint64_t>(sector) * vol->bytesPerSector,
                                             kstd::span(clusterBuf.as<uint8_t>(), clusterSize), transferred);
        status) {
      if (actually_read == 0) return kstd::unexpected(status);
      break;// short read; the error resurfaces on the next call
    }

    uint64_t chunk = clusterSize - offsetInCluster;
    if (chunk > read_size - actually_read) chunk = read_size - actually_read;
    if (transferred <= offsetInCluster) {
      if (actually_read == 0) return kstd::unexpected(kernel::KernelError::UnexpectedEndOfFile);
      break;
    }
    if (chunk > transferred - offsetInCluster) chunk = transferred - offsetInCluster;

    for (uint64_t i = 0; i < chunk; i++) {
      destination[static_cast<size_t>(actually_read + i)] = clusterBuf.as<uint8_t>()[offsetInCluster + i];
    }

    actually_read += chunk;
    if (chunk) {
      lastReadCluster = cluster;
      lastReadIndex = currentIndex;
    }
    if (transferred < clusterSize) break;
    if (actually_read == read_size) break;
    offsetInCluster = 0;
    const auto next = FAT_GetNextCluster(mountPoint, vol, cluster);
    if (!next) {
      if (actually_read == 0) return kstd::unexpected(next.error());
      break;
    }
    cluster = *next;
    if (FAT_IsEndOfChain(vol, cluster)) break;
    ++currentIndex;
  }

  if (actually_read == 0) return kstd::unexpected(kernel::KernelError::UnexpectedEndOfFile);
  if (!vol->traversalReuseDisabled && FAT_ValidCluster(*vol, lastReadCluster)) {
    fatNode->fileChain = {
        .valid = true,
        .generation = vol->traversalGeneration,
        .startCluster = fatNode->startCluster,
        .clusterIndex = lastReadIndex,
        .cluster = lastReadCluster,
    };
  } else {
    fatNode->fileChain.valid = false;
  }
  return actually_read;
}

kstd::error_code FAT_Lookup(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint,
                               const kernel::KVFSNode &parent,
                               kstd::string_view name,
                               kernel::KVFSNode &out) {
  if (parent.type != kernel::VFSNodeType::Directory) { return kernel::KernelError::NotDirectory; }

  kstd::static_string<12> shortName;
  shortName.append("           ");// 11 spaces, will be overwritten below
  if (const auto status = FAT_ToShortName(name, shortName); status) {
    return status;
  }

  const auto *vol = mountPoint->driverExtension.as<FAT_Volume>();
  const auto *dirNode = parent.fsPrivate.as<FAT_VFSNode>();

  const auto sectorBuf = kernel::DataBuffer::Alloc(vol->bytesPerSector);
  if (!sectorBuf) return kernel::KernelError::OutOfMemory;
  const kstd::span sectorSpan(sectorBuf.as<uint8_t>(), vol->bytesPerSector);
  const size_t entriesPerSector = vol->bytesPerSector / sizeof(FAT_DirEntry);

  uint32_t cluster = dirNode->startCluster;
  uint32_t sectorInCluster = 0;
  uint32_t fixedSectorIndex = 0;

  while (true) {
    const auto more = FAT_ReadDirSector(mountPoint, vol, dirNode, cluster, sectorInCluster, fixedSectorIndex, sectorSpan);
    if (!more) return more.error();
    if (!*more) break;

    const auto *entries = reinterpret_cast<const FAT_DirEntry *>(sectorBuf.as<uint8_t>());

    for (size_t i = 0; i < entriesPerSector; i++) {
      const FAT_DirEntry &entry = entries[i];

      if (entry.name[0] == '\0') { return kernel::KernelError::NotFound; }// End of directory
      // entry.name is a fixed 11-byte, space-padded field with no NUL terminator, so it
      // must never be implicitly converted to a string_view via strlen()-style construction
      // (that would read past the field into the following struct members). Build an
      // explicit fixed-length view instead.
      const kstd::string_view entryName(entry.name, sizeof(entry.name));
      if (static_cast<uint8_t>(entry.name[0]) == 0xE5) continue;  // Deleted entry
      if ((entry.attr & kAttrLongName) == kAttrLongName) continue;// Long-name fragment
      if (entry.attr & kAttrVolumeId) continue;                   // Volume label

      if (entryName == shortName) {
        return FAT_FillNode(entry, out);
      }
    }
  }

  return kernel::KernelError::NotFound;
}

kstd::error_code FAT_GetRootNode(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, kernel::KVFSNode &rootOut) {
  const auto *vol = mountPoint->driverExtension.as<FAT_Volume>();

  rootOut.type = kernel::VFSNodeType::Directory;
  rootOut.size = 0;
  rootOut.attributes = kAttrDirectory;
  rootOut.fsPrivate = kernel::DataBuffer::Create<FAT_VFSNode>();
  if (!rootOut.fsPrivate) return kernel::KernelError::OutOfMemory;

  auto *node = rootOut.fsPrivate.as<FAT_VFSNode>();
  if (vol->type == FAT_Type::FAT32 || vol->type == FAT_Type::ExFAT) {
    node->startCluster = vol->rootCluster;
    node->isFixedRoot = false;
  } else {
    node->startCluster = 0;
    node->isFixedRoot = true;
  }

  return kernel::KernelError::Success;
}

#include "fat_operations.hpp"

}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  g_driver = driver;

  if (const auto fs = KE_VFS_RegisterFilesystemDriver(g_driver, "FAT")) {
    (*fs)->MountVolume = FAT_MountVolume;
    (*fs)->ProbeVolumeId = FAT_ProbeVolumeId;
    (*fs)->Lookup = FAT_Lookup;
    (*fs)->GetRootNode = FAT_GetRootNode;
    (*fs)->CreateHandle = FAT_CreateHandle;
    (*fs)->ReadHandle = FAT_ReadHandle;
    (*fs)->ReadDirectory = FAT_ReadDirectory;
    (*fs)->OperateFile = FAT_OperateFile;
  } else {
    KE_Print("FS_FAT: Failed to register filesystem driver");
  }
}
