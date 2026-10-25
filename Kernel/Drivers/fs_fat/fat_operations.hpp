// Included inside fat.cpp's private namespace; all operations run under the VFS volume lock.

void FAT_InvalidateTraversal(FAT_Volume &vol) {
  if (vol.traversalGeneration == UINT64_MAX) {
    vol.traversalReuseDisabled = true;
    return;
  }
  ++vol.traversalGeneration;
}

kstd::error_code FAT_WriteBytes(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount,
    uint64_t offset, kstd::span<const uint8_t> bytes) {
  auto *vol = mount->driverExtension.as<FAT_Volume>();
  FAT_InvalidateTraversal(*vol);
  size_t transferred = 0;
  const auto status = KE_IO_WriteDevice(mount->volume, offset, bytes, transferred);
  if (!status && transferred == bytes.size()) return {};
  vol->writable = false;
  KE_Print("FS_FAT: write failed; volume mutations disabled until remount; filesystem check required");
  return status ? status : kernel::make_error_code(kernel::KernelError::IoError);
}

kstd::error_code FAT_SetCluster(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount,
    const FAT_Volume &vol, uint32_t cluster, uint32_t value) {
  if (!FAT_ValidCluster(vol, cluster) || vol.type == FAT_Type::ExFAT) return kernel::KernelError::InvalidFormat;
  // FAT32's cached free count and next-free hint must not survive allocation changes.
  if (!vol.fsInfoInvalidated) {
    const uint32_t sectors[] = {vol.fsInfoSector, vol.backupFsInfoSector};
    for (const auto sector : sectors) {
      if (!sector) continue;
      uint8_t bytes[512]{};
      size_t transferred = 0;
      const auto address = static_cast<uint64_t>(sector) * vol.bytesPerSector;
      if (const auto status = KE_IO_ReadDevice(mount->volume, address, bytes, transferred); status) return status;
      if (transferred != sizeof(bytes)) return kernel::KernelError::UnexpectedEndOfFile;
      if (bytes[0] != 0x52 || bytes[1] != 0x52 || bytes[2] != 0x61 || bytes[3] != 0x41 ||
          bytes[484] != 0x72 || bytes[485] != 0x72 || bytes[486] != 0x41 || bytes[487] != 0x61) continue;
      for (size_t i = 488; i < 496; ++i) bytes[i] = 0xff;
      if (const auto status = FAT_WriteBytes(mount, address, bytes); status) return status;
    }
    vol.fsInfoInvalidated = true;
  }
  const uint32_t offset = vol.type == FAT_Type::FAT12 ? cluster + cluster / 2 :
      cluster * (vol.type == FAT_Type::FAT16 ? 2u : 4u);
  const size_t width = vol.type == FAT_Type::FAT32 ? 4 : 2;
  for (uint32_t copy = 0; copy < vol.numFATs; ++copy) {
    const uint64_t address = static_cast<uint64_t>(vol.firstFATSector + copy * vol.fatSizeSectors) * vol.bytesPerSector + offset;
    uint8_t bytes[4]{};
    size_t transferred = 0;
    if (const auto status = KE_IO_ReadDevice(mount->volume, address, kstd::span(bytes, width), transferred); status) return status;
    if (transferred != width) return kernel::KernelError::UnexpectedEndOfFile;
    uint32_t raw = bytes[0] | (static_cast<uint32_t>(bytes[1]) << 8);
    if (width == 4) raw |= (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    if (vol.type == FAT_Type::FAT12)
      raw = cluster & 1 ? (raw & 0x000f) | ((value & 0xfff) << 4) : (raw & 0xf000) | (value & 0xfff);
    else if (width == 4) raw = (raw & 0xf0000000) | (value & 0x0fffffff);
    else raw = value & 0xffff;
    for (size_t i = 0; i < width; ++i) bytes[i] = static_cast<uint8_t>(raw >> (i * 8));
    if (const auto status = FAT_WriteBytes(mount, address, kstd::span<const uint8_t>(bytes, width)); status) return status;
  }
  return {};
}

kstd::error_code FAT_FreeChain(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount,
    const FAT_Volume &vol, uint32_t cluster) {
  uint32_t count = 0;
  uint32_t current = cluster;
  // Validate the entire chain before freeing anything, including cycle bounds.
  while (current && !FAT_IsEndOfChain(&vol, current)) {
    if (++count > vol.totalClusters || !FAT_ValidCluster(vol, current)) return kernel::KernelError::InvalidFormat;
    const auto next = FAT_GetNextCluster(mount, &vol, current);
    if (!next) return next.error();
    if (!*next) return kernel::KernelError::InvalidFormat;
    current = *next;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const auto next = FAT_GetNextCluster(mount, &vol, cluster);
    if (!next) return next.error();
    if (const auto status = FAT_SetCluster(mount, vol, cluster, 0); status) return status;
    cluster = *next;
  }
  return {};
}

kstd::expected<uint32_t, kstd::error_code> FAT_AllocateCluster(
    const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const FAT_Volume &vol) {
  const size_t size = static_cast<size_t>(vol.bytesPerSector) * vol.sectorsPerCluster;
  auto zero = kernel::DataBuffer::Alloc(size);
  if (!zero) return kstd::unexpected(kernel::KernelError::OutOfMemory);
  for (size_t i = 0; i < size; ++i) zero.as<uint8_t>()[i] = 0;
  for (uint32_t cluster = 2; cluster - 2 < vol.totalClusters; ++cluster) {
    const auto next = FAT_GetNextCluster(mount, &vol, cluster);
    if (!next) return kstd::unexpected(next.error());
    if (*next) continue;
    if (const auto status = FAT_SetCluster(mount, vol, cluster, 0x0fffffff); status) return kstd::unexpected(status);
    if (const auto status = FAT_WriteBytes(mount,
        static_cast<uint64_t>(FAT_ClusterToSector(&vol, cluster)) * vol.bytesPerSector,
        kstd::span<const uint8_t>(zero.as<uint8_t>(), size)); status) {
      if (const auto cleanup = FAT_SetCluster(mount, vol, cluster, 0); cleanup)
        KE_Print("FS_FAT: allocation cleanup failed: {}", cleanup.value());
      return kstd::unexpected(status);
    }
    return cluster;
  }
  return kstd::unexpected(kernel::KernelError::ResourceLimitExceeded);
}

// Returns a physical sector or zero at end of chain. Bounds prevent corrupt cycles from hanging the kernel.
kstd::expected<uint32_t, kstd::error_code> FAT_DirectorySector(
    const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const FAT_Volume &vol,
    const FAT_VFSNode &dir, uint32_t index, FAT_ChainCheckpoint &checkpoint) {
  if (dir.isFixedRoot) return index < vol.rootDirSectors ? vol.rootDirSector + index : 0u;
  if (index / vol.sectorsPerCluster >= vol.totalClusters) return kstd::unexpected(kernel::KernelError::InvalidFormat);
  const uint64_t targetIndex = index / vol.sectorsPerCluster;
  uint32_t cluster = dir.startCluster;
  uint64_t clusterIndex = 0;
  if (!vol.traversalReuseDisabled && checkpoint.valid &&
      checkpoint.generation == vol.traversalGeneration &&
      checkpoint.startCluster == dir.startCluster &&
      checkpoint.clusterIndex <= targetIndex &&
      FAT_ValidCluster(vol, checkpoint.cluster)) {
    cluster = checkpoint.cluster;
    clusterIndex = checkpoint.clusterIndex;
  }
  for (; clusterIndex < targetIndex; ++clusterIndex) {
    if (!FAT_ValidCluster(vol, cluster)) return kstd::unexpected(kernel::KernelError::InvalidFormat);
    const auto next = FAT_GetNextCluster(mount, &vol, cluster);
    if (!next) return kstd::unexpected(next.error());
    if (FAT_IsEndOfChain(&vol, *next)) return 0u;
    if (!FAT_ValidCluster(vol, *next)) return kstd::unexpected(kernel::KernelError::InvalidFormat);
    cluster = *next;
  }
  if (!FAT_ValidCluster(vol, cluster)) return kstd::unexpected(kernel::KernelError::InvalidFormat);
  if (!vol.traversalReuseDisabled) {
    checkpoint = {
        .valid = true,
        .generation = vol.traversalGeneration,
        .startCluster = dir.startCluster,
        .clusterIndex = clusterIndex,
        .cluster = cluster,
    };
  } else {
    checkpoint.valid = false;
  }
  return FAT_ClusterToSector(&vol, cluster) + index % vol.sectorsPerCluster;
}

struct FAT_EntryLocation {
  FAT_DirEntry entry{};
  uint64_t address{0};
  uint32_t directorySector{0};
  uint32_t slot{0};
  bool longName{false};
};

// name=nullptr enumerates visible entries, empty name finds a reusable slot.
kstd::expected<bool, kstd::error_code> FAT_FindEntry(
    const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const kernel::KVFSNode &parent,
    const char *name, uint32_t ordinal, FAT_EntryLocation &out) {
  const auto &vol = *mount->driverExtension.as<FAT_Volume>();
  if (vol.type == FAT_Type::ExFAT) return kstd::unexpected(kernel::KernelError::Unsupported);
  if (parent.type != kernel::VFSNodeType::Directory) return kstd::unexpected(kernel::KernelError::NotDirectory);
  auto &dir = *parent.fsPrivate.as<FAT_VFSNode>();
  auto buffer = kernel::DataBuffer::Alloc(vol.bytesPerSector);
  if (!buffer) return kstd::unexpected(kernel::KernelError::OutOfMemory);
  const bool enumerate = name == nullptr;
  const uint64_t generation = vol.traversalGeneration;
  if (enumerate && (vol.traversalReuseDisabled ||
      (dir.directoryOrdinal.valid && dir.directoryOrdinal.generation != generation) ||
      (!dir.isFixedRoot && dir.directoryChain.valid && dir.directoryChain.generation != generation) ||
      (dir.directoryEofKnown && dir.directoryEofGeneration != generation))) {
    dir.directoryOrdinal.valid = false;
    dir.directoryChain.valid = false;
    dir.directoryEofKnown = false;
  }
  if (enumerate && dir.directoryEofKnown && dir.directoryEofGeneration == generation &&
      ordinal >= dir.directoryEofCount) return false;

  uint64_t seen = 0;
  bool longName = false;
  uint32_t sectorIndex = 0;
  uint32_t firstSlot = 0;
  FAT_ChainCheckpoint chain = {};
  if (enumerate && !vol.traversalReuseDisabled && dir.directoryOrdinal.valid &&
      dir.directoryOrdinal.generation == generation && dir.directoryOrdinal.ordinal <= ordinal) {
    sectorIndex = dir.directoryOrdinal.sector;
    firstSlot = dir.directoryOrdinal.slot;
    seen = dir.directoryOrdinal.ordinal;
    longName = dir.directoryOrdinal.pendingLongName;
    chain = dir.directoryChain;
  }

  for (;;) {
    auto nextChain = chain;
    const auto sector = FAT_DirectorySector(mount, vol, dir, sectorIndex, nextChain);
    if (!sector) return kstd::unexpected(sector.error());
    if (!*sector) {
      if (enumerate) {
        dir.directoryEofKnown = true;
        dir.directoryEofCount = seen;
        dir.directoryEofGeneration = generation;
      }
      return false;
    }
    size_t read = 0;
    const auto status = KE_IO_ReadDevice(mount->volume, static_cast<uint64_t>(*sector) * vol.bytesPerSector,
        kstd::span(buffer.as<uint8_t>(), vol.bytesPerSector), read);
    if (status) return kstd::unexpected(status);
    if (read != vol.bytesPerSector) return kstd::unexpected(kernel::KernelError::UnexpectedEndOfFile);
    const auto *entries = buffer.as<FAT_DirEntry>();
    chain = nextChain;
    for (uint32_t slot = firstSlot; slot < vol.bytesPerSector / sizeof(FAT_DirEntry); ++slot) {
      const auto &entry = entries[slot];
      const bool unused = !entry.name[0] || static_cast<uint8_t>(entry.name[0]) == 0xe5;
      bool match = name && !name[0] && unused;
      if (!match && !entry.name[0]) {
        if (enumerate) {
          dir.directoryEofKnown = true;
          dir.directoryEofCount = seen;
          dir.directoryEofGeneration = generation;
        }
        return false;
      }
      if (!unused && entry.attr == kAttrLongName) { longName = true; continue; }
      if (!unused && !(entry.attr & kAttrVolumeId) && entry.name[0] != '.') {
        if (enumerate) {
          if (seen == ordinal) {
            out = {.entry = entry, .address = static_cast<uint64_t>(*sector) * vol.bytesPerSector + slot * sizeof(FAT_DirEntry),
                   .directorySector = sectorIndex, .slot = slot, .longName = longName};
            if (!vol.traversalReuseDisabled) {
              dir.directoryOrdinal = {
                  .valid = true,
                  .generation = generation,
                  .ordinal = seen,
                  .sector = sectorIndex,
                  .slot = slot,
                  .pendingLongName = longName,
              };
              dir.directoryChain = chain;
              if (dir.isFixedRoot) dir.directoryChain.generation = generation;
            } else {
              dir.directoryOrdinal.valid = false;
              dir.directoryChain.valid = false;
            }
            dir.directoryEofKnown = false;
            return true;
          }
          ++seen;
        } else {
          match = name[0] && kstd::string_view(entry.name, 11) == kstd::string_view(name, 11);
        }
      }
      if (match) {
        out = {.entry = entry, .address = static_cast<uint64_t>(*sector) * vol.bytesPerSector + slot * sizeof(FAT_DirEntry),
               .directorySector = sectorIndex, .slot = slot, .longName = longName};
        return true;
      }
      longName = false;
    }
    firstSlot = 0;
    if (sectorIndex == UINT32_MAX) return kstd::unexpected(kernel::KernelError::InvalidFormat);
    ++sectorIndex;
  }
}

kstd::error_code FAT_WriteEntry(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount,
    const FAT_EntryLocation &location, const FAT_DirEntry &entry) {
  return FAT_WriteBytes(mount, location.address,
      kstd::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&entry), sizeof(entry)));
}

kstd::error_code FAT_ShortName(kstd::string_view name, char (&out)[12]) {
  if (name.empty() || name == "." || name == ".." || name.back() == '.') return kernel::KernelError::InvalidArgument;
  for (char c : name) {
    if (static_cast<uint8_t>(c) <= 32 || static_cast<uint8_t>(c) >= 127 ||
        c == '"' || c == '*' || c == '+' || c == ',' || c == '/' || c == ':' ||
        c == ';' || c == '<' || c == '=' || c == '>' || c == '?' ||
        c == '[' || c == '\\' || c == ']' || c == '|') return kernel::KernelError::InvalidArgument;
  }
  if (name[0] == '.' || (name.find('.') != name.rfind('.'))) return kernel::KernelError::InvalidArgument;
  return FAT_ToShortName(name, kstd::string_view(out, 11));
}

kstd::expected<bool, kstd::error_code> FAT_ReadDirectory(
    const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const kernel::KVFSNode &parent,
    uint32_t index, kernel::KVFSDirectoryEntry &out) {
  FAT_EntryLocation location;
  const auto found = FAT_FindEntry(mount, parent, nullptr, index, location);
  if (!found || !*found) return found;
  out = {};
  size_t n = 0;
  for (size_t i = 0; i < 8 && location.entry.name[i] != ' '; ++i) out.name[n++] = location.entry.name[i];
  if (location.entry.name[8] != ' ') {
    out.name[n++] = '.';
    for (size_t i = 8; i < 11 && location.entry.name[i] != ' '; ++i) out.name[n++] = location.entry.name[i];
  }
  out.size = location.entry.fileSize;
  out.attributes = location.entry.attr;
  return true;
}

kstd::expected<FAT_EntryLocation, kstd::error_code> FAT_FreeSlot(
    const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const kernel::KVFSNode &parent) {
  FAT_EntryLocation slot;
  const auto found = FAT_FindEntry(mount, parent, "", 0, slot);
  if (!found) return kstd::unexpected(found.error());
  const auto &vol = *mount->driverExtension.as<FAT_Volume>();
  const auto &dir = *parent.fsPrivate.as<FAT_VFSNode>();
  if (!*found) {
    if (dir.isFixedRoot) return kstd::unexpected(kernel::KernelError::ResourceLimitExceeded);
    uint32_t tail = dir.startCluster;
    uint32_t steps = 0;
    for (;; ++steps) {
      if (steps >= vol.totalClusters || !FAT_ValidCluster(vol, tail)) return kstd::unexpected(kernel::KernelError::InvalidFormat);
      const auto next = FAT_GetNextCluster(mount, &vol, tail);
      if (!next) return kstd::unexpected(next.error());
      if (FAT_IsEndOfChain(&vol, *next)) break;
      tail = *next;
    }
    const auto fresh = FAT_AllocateCluster(mount, vol);
    if (!fresh) return kstd::unexpected(fresh.error());
    if (const auto status = FAT_SetCluster(mount, vol, tail, *fresh); status) {
      // A failed mirrored FAT write may already have linked the new cluster.
      // Do not free a cluster that might now be reachable.
      return kstd::unexpected(status);
    }
    slot.address = static_cast<uint64_t>(FAT_ClusterToSector(&vol, *fresh)) * vol.bytesPerSector;
    return slot;
  }
  if (!slot.entry.name[0]) {
    FAT_DirEntry end{};
    const uint32_t entries = vol.bytesPerSector / sizeof(FAT_DirEntry);
    uint64_t nextAddress = slot.address + sizeof(FAT_DirEntry);
    if (slot.slot + 1 == entries) {
      FAT_ChainCheckpoint checkpoint{};
      const auto nextSector = FAT_DirectorySector(mount, vol, dir, slot.directorySector + 1, checkpoint);
      if (!nextSector) return kstd::unexpected(nextSector.error());
      if (!*nextSector) return slot;
      nextAddress = static_cast<uint64_t>(*nextSector) * vol.bytesPerSector;
    }
    if (const auto status = FAT_WriteBytes(mount, nextAddress,
        kstd::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&end), sizeof(end))); status)
      return kstd::unexpected(status);
  }
  return slot;
}

uint32_t FAT_EntryCluster(const FAT_DirEntry &entry) {
  return (static_cast<uint32_t>(entry.firstClusterHigh) << 16) | entry.firstClusterLow;
}

void FAT_StoreCluster(FAT_DirEntry &entry, uint32_t cluster) {
  entry.firstClusterLow = static_cast<uint16_t>(cluster);
  entry.firstClusterHigh = static_cast<uint16_t>(cluster >> 16);
}

kstd::error_code FAT_OperateFile(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount,
    kernel::VFSFileOperation operation, const kernel::KVFSNode &parent, kstd::string_view name,
    const kernel::KVFSNode *destinationParent, kstd::string_view destinationName) {
  using kernel::KernelError;
  using kernel::VFSFileOperation;
  const auto &vol = *mount->driverExtension.as<FAT_Volume>();
  if (vol.type == FAT_Type::ExFAT || !vol.writable) return KernelError::Unsupported;
  if ((parent.attributes & kAttrReadOnly) || (destinationParent && (destinationParent->attributes & kAttrReadOnly)))
    return KernelError::AccessDenied;
  char shortName[12]{}, targetName[12]{};
  if (const auto status = FAT_ShortName(name, shortName); status) return status;
  FAT_EntryLocation source;
  const auto found = FAT_FindEntry(mount, parent, shortName, 0, source);
  if (!found) return found.error();
  if (operation == VFSFileOperation::CreateDirectory) {
    if (*found) return KernelError::AlreadyExists;
  } else {
    if (!*found) return KernelError::NotFound;
    if (source.longName) return KernelError::Unsupported;
    if ((source.entry.attr & kAttrReadOnly) && operation != VFSFileOperation::Copy) return KernelError::AccessDenied;
  }
  const bool isDirectory = source.entry.attr & kAttrDirectory;
  if (operation == VFSFileOperation::Delete) {
    if (isDirectory) {
      kernel::KVFSNode directory{};
      if (const auto status = FAT_FillNode(source.entry, directory); status) return status;
      FAT_EntryLocation child;
      const auto any = FAT_FindEntry(mount, directory, nullptr, 0, child);
      if (!any) return any.error();
      if (*any) return KernelError::AccessDenied;
    }
    auto deleted = source.entry;
    deleted.name[0] = static_cast<char>(0xe5);
    if (const auto status = FAT_WriteEntry(mount, source, deleted); status) return status;
    return FAT_FreeChain(mount, vol, FAT_EntryCluster(source.entry));
  }
  const kernel::KVFSNode &targetParent = destinationParent ? *destinationParent : parent;
  if (destinationParent) {
    if (const auto status = FAT_ShortName(destinationName, targetName); status) return status;
    FAT_EntryLocation existing;
    const auto exists = FAT_FindEntry(mount, targetParent, targetName, 0, existing);
    if (!exists) return exists.error();
    if (*exists) return KernelError::AlreadyExists;
  } else {
    for (size_t i = 0; i < 11; ++i) targetName[i] = shortName[i];
  }
  const auto &srcDir = *parent.fsPrivate.as<FAT_VFSNode>();
  const auto &dstDir = *targetParent.fsPrivate.as<FAT_VFSNode>();
  const bool sameParent = srcDir.isFixedRoot == dstDir.isFixedRoot && srcDir.startCluster == dstDir.startCluster;
  if (operation == VFSFileOperation::Move && sameParent) {
    auto renamed = source.entry;
    for (size_t i = 0; i < 11; ++i) renamed.name[i] = targetName[i];
    return FAT_WriteEntry(mount, source, renamed);
  }
  if (isDirectory && operation != VFSFileOperation::CreateDirectory) return KernelError::Unsupported;
  const auto slot = FAT_FreeSlot(mount, targetParent);
  if (!slot) return slot.error();
  FAT_DirEntry created = operation == VFSFileOperation::CreateDirectory ? FAT_DirEntry{} : source.entry;
  for (size_t i = 0; i < 11; ++i) created.name[i] = targetName[i];
  created.ntReserved = 0;
  if (operation == VFSFileOperation::Move) {
    if (const auto status = FAT_WriteEntry(mount, *slot, created); status) return status;
    auto deleted = source.entry;
    deleted.name[0] = static_cast<char>(0xe5);
    if (const auto status = FAT_WriteEntry(mount, source, deleted); status) {
      KE_Print("FS_FAT: interrupted move may have duplicate entries; check filesystem before further writes");
      return status;
    }
    return {};
  }
  uint32_t first = 0, tail = 0;
  const auto cleanup = [&](kstd::error_code failure) {
    if (const auto status = FAT_FreeChain(mount, vol, first); status)
      KE_Print("FS_FAT: unpublished chain cleanup failed: {}", status.value());
    return failure;
  };
  const size_t clusterBytes = static_cast<size_t>(vol.bytesPerSector) * vol.sectorsPerCluster;
  auto buffer = kernel::DataBuffer::Alloc(clusterBytes);
  if (!buffer) return KernelError::OutOfMemory;
  uint32_t sourceCluster = FAT_EntryCluster(source.entry);
  uint64_t remaining = operation == VFSFileOperation::CreateDirectory ? clusterBytes : source.entry.fileSize;
  for (uint32_t steps = 0; remaining; ++steps) {
    if (steps >= vol.totalClusters) return cleanup(KernelError::InvalidFormat);
    for (size_t i = 0; i < clusterBytes; ++i) buffer.as<uint8_t>()[i] = 0;
    if (operation == VFSFileOperation::Copy) {
      if (!FAT_ValidCluster(vol, sourceCluster)) return cleanup(KernelError::InvalidFormat);
      size_t read = 0;
      if (const auto status = KE_IO_ReadDevice(mount->volume,
          static_cast<uint64_t>(FAT_ClusterToSector(&vol, sourceCluster)) * vol.bytesPerSector,
          kstd::span(buffer.as<uint8_t>(), clusterBytes), read); status) return cleanup(status);
      if (read != clusterBytes) return cleanup(KernelError::UnexpectedEndOfFile);
      if (remaining < clusterBytes)
        for (size_t i = static_cast<size_t>(remaining); i < clusterBytes; ++i) buffer.as<uint8_t>()[i] = 0;
    }
    const auto fresh = FAT_AllocateCluster(mount, vol);
    if (!fresh) return cleanup(fresh.error());
    if (!first) first = *fresh;
    if (tail) {
      if (const auto status = FAT_SetCluster(mount, vol, tail, *fresh); status) {
        // Preserve both allocations if the link's on-disk state is uncertain.
        return status;
      }
    }
    tail = *fresh;
    if (operation == VFSFileOperation::CreateDirectory) {
      auto *dots = buffer.as<FAT_DirEntry>();
      for (size_t i = 0; i < 11; ++i) dots[0].name[i] = dots[1].name[i] = ' ';
      dots[0].name[0] = dots[1].name[0] = '.';
      dots[1].name[1] = '.';
      dots[0].attr = dots[1].attr = kAttrDirectory;
      FAT_StoreCluster(dots[0], first);
      FAT_StoreCluster(dots[1], srcDir.isFixedRoot || srcDir.startCluster == vol.rootCluster ? 0 : srcDir.startCluster);
      created.attr = kAttrDirectory;
    }
    if (const auto status = FAT_WriteBytes(mount,
        static_cast<uint64_t>(FAT_ClusterToSector(&vol, *fresh)) * vol.bytesPerSector,
        kstd::span<const uint8_t>(buffer.as<uint8_t>(), clusterBytes)); status) return cleanup(status);
    remaining = remaining > clusterBytes ? remaining - clusterBytes : 0;
    if (remaining) {
      const auto next = FAT_GetNextCluster(mount, &vol, sourceCluster);
      if (!next) return cleanup(next.error());
      sourceCluster = *next;
    }
  }
  FAT_StoreCluster(created, first);
  // Once publication is attempted, a short device write may have made the entry
  // reachable. Never free its chain on that uncertain result.
  return FAT_WriteEntry(mount, *slot, created);
}
