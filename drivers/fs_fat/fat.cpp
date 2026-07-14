
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
  // maybe: pointers to on‑disk FSInfo, etc.
};

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
        exfat_transfer >= exfatBuf.size()) {
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

  mountPoint->driverExtension = kernel::DataBuffer::Create<FAT_Volume>();
  auto vol = mountPoint->driverExtension.as<FAT_Volume>();

  vol->bytesPerSector = bpb->bytesPerSector;
  vol->sectorsPerCluster = bpb->sectorsPerCluster;
  vol->reservedSectors = bpb->reservedSectors;
  vol->numFATs = bpb->numFATs;
  vol->fatSizeSectors = fatSize;
  vol->firstFATSector = bpb->reservedSectors;
  vol->totalClusters = totalClusters;

  if (totalClusters < 4085) {
    vol->type = FAT_Type::FAT12;
    vol->rootDirSector = bpb->reservedSectors + bpb->numFATs * fatSize;
    vol->rootDirSectors = rootDirSectors;
    vol->dataStartSector = vol->rootDirSector + rootDirSectors;
    early_print_fmt("FS_FAT: FAT12 Detected\r\n");
  } else if (totalClusters < 65525) {
    vol->type = FAT_Type::FAT16;
    vol->rootDirSector = bpb->reservedSectors + bpb->numFATs * fatSize;
    vol->rootDirSectors = rootDirSectors;
    vol->dataStartSector = vol->rootDirSector + rootDirSectors;
    early_print_fmt("FS_FAT: FAT16 Detected\r\n");
  } else {
    vol->type = FAT_Type::FAT32;
    vol->rootCluster = fat32->rootCluster;
    vol->dataStartSector = bpb->reservedSectors + bpb->numFATs * fatSize;
    early_print_fmt("FS_FAT: FAT32 Detected\r\n");
  }


  return true;
}

struct FAT_VFSNode {
  uint32_t startCluster = 0;// 0 => fixed-size FAT12/16 root directory area
  bool isFixedRoot = false;
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

uint32_t FAT_ClusterToSector(const FAT_Volume *vol, uint32_t cluster) {
  return vol->dataStartSector + (cluster - kFatFirstDataCluster) * vol->sectorsPerCluster;
}

// Reads the FAT table entry for `cluster` and returns the next cluster in the chain
// (or an end-of-chain marker, see FAT_IsEndOfChain).
uint32_t FAT_GetNextCluster(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const FAT_Volume *vol, uint32_t cluster) {
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
  size_t transferred = 0;
  if (KE_IO_ReadDevice(mountPoint->volume, static_cast<uint64_t>(fatSector) * vol->bytesPerSector,
                       kstd::span(buf.as<uint8_t>(), buf.size), transferred) != kernel::IoStatus::Success) {
    return kFatEndOfChainThreshold32;
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
// a regular cluster chain). Returns false once there is nothing left to read.
bool FAT_ReadDirSector(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const FAT_Volume *vol,
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
      cluster = FAT_GetNextCluster(mountPoint, vol, cluster);
    }
  }

  size_t transferred = 0;
  return KE_IO_ReadDevice(mountPoint->volume, static_cast<uint64_t>(sector) * vol->bytesPerSector, buffer, transferred) == kernel::IoStatus::Success;
}

void FAT_FillNode(const FAT_DirEntry &entry, kernel::KVFSNode &out) {
  const uint32_t startCluster = (static_cast<uint32_t>(entry.firstClusterHigh) << 16) | entry.firstClusterLow;

  out.type = (entry.attr & kAttrDirectory) ? kernel::VFSNodeType::Directory : kernel::VFSNodeType::File;
  out.size = (entry.attr & kAttrDirectory) ? 0 : entry.fileSize;
  out.fsPrivate = kernel::DataBuffer::Create<FAT_VFSNode>();
  auto *node = out.fsPrivate.as<FAT_VFSNode>();
  node->startCluster = startCluster;
  node->isFixedRoot = false;
}

bool FAT_CreateHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const kernel::KVFSNode &vnode, kernel::KFileObject::OpenMode mode) {
  (void) mountPoint;
  (void) mode;

  // FAT can only open Files
  if (vnode.type != kernel::VFSNodeType::File) return false;

  return true;
}

uint64_t FAT_ReadHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const kernel::KVFSNode &node, uint64_t offset, const kstd::span<uint8_t> &destination) {
  uint64_t actually_read = 0;
  uint64_t read_size = destination.size();

  if (node.type != kernel::VFSNodeType::File) return 0;
  if (offset >= node.size) return 0;
  if (offset + read_size > node.size) read_size = node.size - offset;
  if (read_size == 0) return 0;

  const auto *vol = mountPoint->driverExtension.as<FAT_Volume>();
  const auto *fatNode = node.fsPrivate.as<FAT_VFSNode>();
  const uint32_t clusterSize = vol->bytesPerSector * vol->sectorsPerCluster;

  if (fatNode->startCluster == 0 || clusterSize == 0) return 0;

  uint32_t cluster = fatNode->startCluster;
  uint64_t clusterIndex = offset / clusterSize;
  const uint64_t offsetInFirstCluster = offset % clusterSize;

  // Skip forward to the cluster containing `offset`.
  for (uint64_t i = 0; i < clusterIndex; i++) {
    cluster = FAT_GetNextCluster(mountPoint, vol, cluster);
    if (FAT_IsEndOfChain(vol, cluster)) return 0;
  }

  auto clusterBuf = kernel::DataBuffer::Alloc(clusterSize);
  uint64_t offsetInCluster = offsetInFirstCluster;

  while (actually_read < read_size) {
    if (FAT_IsEndOfChain(vol, cluster)) break;

    size_t transferred = 0;
    const uint32_t sector = FAT_ClusterToSector(vol, cluster);
    if (KE_IO_ReadDevice(mountPoint->volume, static_cast<uint64_t>(sector) * vol->bytesPerSector,
                         kstd::span(clusterBuf.as<uint8_t>(), clusterSize), transferred) != kernel::IoStatus::Success) {
      break;
    }

    uint64_t chunk = clusterSize - offsetInCluster;
    if (chunk > read_size - actually_read) chunk = read_size - actually_read;

    for (uint64_t i = 0; i < chunk; i++) {
      destination[static_cast<size_t>(actually_read + i)] = clusterBuf.as<uint8_t>()[offsetInCluster + i];
    }

    actually_read += chunk;
    offsetInCluster = 0;
    cluster = FAT_GetNextCluster(mountPoint, vol, cluster);
  }

  return actually_read;
}

bool FAT_Lookup(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint,
                const kernel::KVFSNode &parent,
                kstd::string_view name,
                kernel::KVFSNode &out) {
  if (parent.type != kernel::VFSNodeType::Directory) { return false; }

  kstd::static_string<12> shortName;
  shortName.append("           ");// 11 spaces, will be overwritten below
  if (!FAT_ToShortName(name, shortName)) {
    return false;
  }

  const auto *vol = mountPoint->driverExtension.as<FAT_Volume>();
  const auto *dirNode = parent.fsPrivate.as<FAT_VFSNode>();

  const auto sectorBuf = kernel::DataBuffer::Alloc(vol->bytesPerSector);
  const kstd::span sectorSpan(sectorBuf.as<uint8_t>(), vol->bytesPerSector);
  const size_t entriesPerSector = vol->bytesPerSector / sizeof(FAT_DirEntry);

  uint32_t cluster = dirNode->startCluster;
  uint32_t sectorInCluster = 0;
  uint32_t fixedSectorIndex = 0;

  while (FAT_ReadDirSector(mountPoint, vol, dirNode, cluster, sectorInCluster, fixedSectorIndex, sectorSpan)) {
    const auto *entries = reinterpret_cast<const FAT_DirEntry *>(sectorBuf.as<uint8_t>());

    for (size_t i = 0; i < entriesPerSector; i++) {
      const FAT_DirEntry &entry = entries[i];

      if (entry.name[0] == '\0') { return false; }// End of directory
      // entry.name is a fixed 11-byte, space-padded field with no NUL terminator, so it
      // must never be implicitly converted to a string_view via strlen()-style construction
      // (that would read past the field into the following struct members). Build an
      // explicit fixed-length view instead.
      const kstd::string_view entryName(entry.name, sizeof(entry.name));
      if (static_cast<uint8_t>(entry.name[0]) == 0xE5) continue;  // Deleted entry
      if ((entry.attr & kAttrLongName) == kAttrLongName) continue;// Long-name fragment
      if (entry.attr & kAttrVolumeId) continue;                   // Volume label

      if (entryName == shortName) {
        FAT_FillNode(entry, out);
        return true;
      }
    }
  }

  return false;
}

bool FAT_GetRootNode(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, kernel::KVFSNode &rootOut) {
  const auto *vol = mountPoint->driverExtension.as<FAT_Volume>();

  rootOut.type = kernel::VFSNodeType::Directory;
  rootOut.size = 0;
  rootOut.fsPrivate = kernel::DataBuffer::Create<FAT_VFSNode>();

  auto *node = rootOut.fsPrivate.as<FAT_VFSNode>();
  if (vol->type == FAT_Type::FAT32 || vol->type == FAT_Type::ExFAT) {
    node->startCluster = vol->rootCluster;
    node->isFixedRoot = false;
  } else {
    node->startCluster = 0;
    node->isFixedRoot = true;
  }

  return true;
}

}// namespace

UNDOS_DRIVER_ENTRY {
  g_driver = driver;

  if (const auto fs = KE_VFS_RegisterFilesystemDriver(g_driver, "FAT")) {
    fs->MountVolume = FAT_MountVolume;
    fs->Lookup = FAT_Lookup;
    fs->GetRootNode = FAT_GetRootNode;
    fs->CreateHandle = FAT_CreateHandle;
    fs->ReadHandle = FAT_ReadHandle;
  }
}
