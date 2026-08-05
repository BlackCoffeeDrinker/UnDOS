
#include "configuration_manager_io.hpp"
#include "stdkrn.hpp"

namespace kernel::configuration_manager_io {

bool ReadRawAt(const kernel::KObjectPtr<kernel::KFileObject> &file, uint64_t offset, kstd::span<uint8_t> outValue) {
  if (!file) return false;
  KE_VFS_SeekFile(file, offset);
  return KE_VFS_ReadFile(file, outValue) == outValue.size();
}

bool WriteRawAt(const kernel::KObjectPtr<kernel::KFileObject> &file, uint64_t offset, kstd::span<const uint8_t> data) {
  if (!file) return false;
  KE_VFS_SeekFile(file, offset);
  return KE_VFS_WriteFile(file, data) == data.size();
}

uint64_t PairedNameBlockOffset(uint32_t indexBlockOffset) {
  return static_cast<uint64_t>(indexBlockOffset) + sizeof(ConfigurationIndexBlock);
}

void ReadNameEntry(const kernel::KObjectPtr<kernel::KFileObject> &file, uint32_t indexBlockOffset, uint16_t nameEntryIndex, uint64_t keyHash, kstd::static_string<32> &outName) {
  if (nameEntryIndex == NO_NAME_ENTRY || nameEntryIndex >= ENTRIES_PER_NAME_BLOCK) return;

  const uint64_t entryOffset = PairedNameBlockOffset(indexBlockOffset) + offsetof(ConfigurationNameBlock, entries) +
                               static_cast<uint64_t>(nameEntryIndex) * sizeof(ConfigurationNameEntry);
  ConfigurationNameEntry entry{};
  kstd::span entryData(reinterpret_cast<uint8_t *>(&entry), sizeof(ConfigurationNameEntry));
  if (!ReadRawAt(file, entryOffset, entryData)) return;
  if (entry.keyHash != keyHash) return;

  outName = kstd::string_view(entry.name, kstd::char_traits<char>::length(entry.name));
}

uint64_t ComputeDataRegionEnd(const kernel::KConfigurationRoot &root, uint32_t indexBlockOffset) {
  uint64_t end = indexBlockOffset + BLOCK_PAIR_BYTES;
  for (const auto &entry: root.roots) {
    if (entry.indexBlockOffset != indexBlockOffset) continue;
    const uint64_t entryEnd = static_cast<uint64_t>(entry.offset) + entry.length;
    if (entryEnd > end) end = entryEnd;
  }
  return end;
}

bool PersistIndexEntry(const kernel::KObjectPtr<kernel::KFileObject> &file, const kernel::KConfigurationRoot::Root &root) {
  const ConfigurationIndexEntry onDisk{
      .keyHash = root.keyHash,
      .parentHash = root.parentHash,
      .nameEntryIndex = root.nameEntryIndex,
      .valueType = static_cast<uint8_t>(root.valueType),
      .reserved0 = 0,
      .offset = root.offset,
      .length = root.length};
  return WriteRawAt(file, root.indexEntryOffset,
                    kstd::span(reinterpret_cast<const uint8_t *>(&onDisk), sizeof(ConfigurationIndexEntry)));
}

bool PersistNameEntry(const kernel::KObjectPtr<kernel::KFileObject> &file, uint32_t indexBlockOffset, uint16_t nameEntryIndex, uint64_t keyHash, const kstd::string_view &name) {
  ConfigurationNameEntry onDisk{};
  onDisk.keyHash = keyHash;
  const size_t toCopy = name.size() < NAME_LENGTH ? name.size() : NAME_LENGTH;
  for (size_t i = 0; i < toCopy; i++) {
    onDisk.name[i] = name[i];
  }

  const uint64_t entryOffset = PairedNameBlockOffset(indexBlockOffset) + offsetof(ConfigurationNameBlock, entries) +
                               static_cast<uint64_t>(nameEntryIndex) * sizeof(ConfigurationNameEntry);
  return WriteRawAt(file, entryOffset, kstd::span(reinterpret_cast<const uint8_t *>(&onDisk), sizeof(ConfigurationNameEntry)));
}

bool FindOrAllocateNameSlot(const kernel::KObjectPtr<kernel::KFileObject> &file, uint32_t indexBlockOffset, uint64_t keyHash, const kstd::string_view &name, uint16_t &outNameEntryIndex) {
  const uint64_t nameBlockOffset = PairedNameBlockOffset(indexBlockOffset);

  for (uint16_t slot = 0; slot < ENTRIES_PER_NAME_BLOCK; slot++) {
    uint64_t existingHash = 0;
    const uint64_t entryOffset = nameBlockOffset + offsetof(ConfigurationNameBlock, entries) + static_cast<uint64_t>(slot) * sizeof(ConfigurationNameEntry);
    if (!ReadRawAt(file, entryOffset + offsetof(ConfigurationNameEntry, keyHash),
                   kstd::span(reinterpret_cast<uint8_t *>(&existingHash), sizeof(existingHash)))) {
      return false;
    }

    if (existingHash == 0) {
      if (!PersistNameEntry(file, indexBlockOffset, slot, keyHash, name)) {
        return false;
      }
      outNameEntryIndex = slot;
      return true;
    }
  }
  return false;
}

bool ReadKeyValue(const kernel::KConfigurationRoot &root, uint64_t keyHash, uint64_t parentHash, kernel::ConfigurationValueType expectedType, kstd::span<uint8_t> outValue) {
  for (const auto &entry: root.roots) {
    if (entry.keyHash == keyHash && entry.parentHash == parentHash) {
      if (entry.valueType != expectedType || entry.length != outValue.size()) {
        return false;
      }

      return ReadRawAt(root.file, entry.offset, outValue);
    }
  }
  return false;
}

bool FindOrAllocateFreeSlot(kernel::KConfigurationRoot &root, uint32_t &outIndexBlockOffset, uint32_t &outIndexEntryOffset) {
  uint64_t indexBlockOffset = sizeof(ConfigurationFileHeader);

  while (true) {
    size_t occupied = 0;
    for (const auto &entry: root.roots) {
      if (entry.indexBlockOffset == indexBlockOffset) occupied++;
    }

    if (occupied < ENTRIES_PER_INDEX_BLOCK) {
      outIndexBlockOffset = static_cast<uint32_t>(indexBlockOffset);
      outIndexEntryOffset = static_cast<uint32_t>(indexBlockOffset + offsetof(ConfigurationIndexBlock, entries) + occupied * sizeof(ConfigurationIndexEntry));
      return true;
    }

    uint32_t nextBlockOffset = 0;
    kstd::span nextBlockOffsetData(reinterpret_cast<uint8_t *>(&nextBlockOffset), sizeof(nextBlockOffset));
    if (!ReadRawAt(root.file, indexBlockOffset + offsetof(ConfigurationIndexBlock, nextBlockOffset), nextBlockOffsetData)) {
      return false;
    }

    if (nextBlockOffset != 0) {
      indexBlockOffset = nextBlockOffset;
      continue;
    }

    // This is the last block pair in the chain and its INDEX BLOCK is full:
    // append a brand-new (empty) INDEX BLOCK/NAME BLOCK pair right after
    // this pair's data region, and link the two INDEX BLOCKs together.
    const uint64_t newIndexBlockOffset = ComputeDataRegionEnd(root, static_cast<uint32_t>(indexBlockOffset));
    const ConfigurationIndexBlock freshIndexBlock{};
    const ConfigurationNameBlock freshNameBlock{};
    if (!WriteRawAt(root.file, newIndexBlockOffset,
                    kstd::span(reinterpret_cast<const uint8_t *>(&freshIndexBlock), sizeof(ConfigurationIndexBlock))) ||
        !WriteRawAt(root.file, newIndexBlockOffset + sizeof(ConfigurationIndexBlock),
                    kstd::span(reinterpret_cast<const uint8_t *>(&freshNameBlock), sizeof(ConfigurationNameBlock)))) {
      return false;
    }

    const auto newIndexBlockOffset32 = static_cast<uint32_t>(newIndexBlockOffset);
    if (!WriteRawAt(root.file, indexBlockOffset + offsetof(ConfigurationIndexBlock, nextBlockOffset),
                    kstd::span(reinterpret_cast<const uint8_t *>(&newIndexBlockOffset32), sizeof(newIndexBlockOffset32)))) {
      return false;
    }

    indexBlockOffset = newIndexBlockOffset;
  }
}

bool WriteKeyValue(kernel::KConfigurationRoot &root, uint64_t keyHash, uint64_t parentHash, kernel::ConfigurationValueType type, kstd::span<const uint8_t> data) {
  for (auto &entry: root.roots) {
    if (entry.keyHash == keyHash && entry.parentHash == parentHash) {
      uint64_t targetOffset = entry.offset;
      if (data.size() > entry.length) {
        // Doesn't fit in the space it previously used: relocate to the end
        // of this entry's own block pair's data region instead of
        // overwriting (the old bytes are left behind, unreachable, until
        // the hive is compacted).
        targetOffset = ComputeDataRegionEnd(root, entry.indexBlockOffset);
      }

      if (!WriteRawAt(root.file, targetOffset, data)) {
        return false;
      }

      entry.offset = static_cast<uint32_t>(targetOffset);
      entry.length = static_cast<uint32_t>(static_cast<uint64_t>(data.size()));
      entry.valueType = type;

      return PersistIndexEntry(root.file, entry);
    }
  }
  return false;
}

bool FormatEmptyConfigurationFile(const kernel::KObjectPtr<kernel::KFileObject> &file) {
  if (!file) return false;

  constexpr uint32_t indexBlockOffset = sizeof(ConfigurationFileHeader);
  constexpr uint32_t nameBlockOffset = indexBlockOffset + sizeof(ConfigurationIndexBlock);
  constexpr auto dataRegionOffset = static_cast<uint32_t>(indexBlockOffset + BLOCK_PAIR_BYTES);

  const ConfigurationFileHeader header{
      .indexEntryCount = 0,
      .nameEntryCount = 0,
      .offsetToFirstIndexBlock = indexBlockOffset,
      .offsetToFirstNameBlock = nameBlockOffset,
      .offsetToDataRegion = dataRegionOffset};
  if (!WriteRawAt(file, 0, kstd::span(reinterpret_cast<const uint8_t *>(&header), sizeof(header)))) {
    return false;
  }

  const ConfigurationIndexBlock emptyIndexBlock{};
  if (!WriteRawAt(file, indexBlockOffset,
                  kstd::span(reinterpret_cast<const uint8_t *>(&emptyIndexBlock), sizeof(emptyIndexBlock)))) {
    return false;
  }

  const ConfigurationNameBlock emptyNameBlock{};
  return WriteRawAt(file, nameBlockOffset,
                    kstd::span(reinterpret_cast<const uint8_t *>(&emptyNameBlock), sizeof(emptyNameBlock)));
}

bool LoadConfigurationIndex(const kernel::KObjectPtr<kernel::KFileObject> &file, kstd::list<kernel::KConfigurationRoot::Root> &outRoots, size_t &outNumRoots) {
  ConfigurationFileHeader header{};
  {
    const kstd::span headerData(reinterpret_cast<uint8_t *>(&header), sizeof(ConfigurationFileHeader));
    if (!ReadRawAt(file, 0, headerData) || header.magic != MAGIC) {
      return false;
    }
  }

  // Read only the (lightweight) index into memory; the actual value
  // payloads stay on disk and are read on demand when a value is queried.
  // The on-disk index is a linked chain of fixed-size INDEX BLOCKs (127
  // entries each, immediately followed by their paired NAME BLOCK): walk
  // the chain via each block's absolute `nextBlockOffset`, collecting every
  // populated slot (keyHash != 0) until a block's next-block offset is 0.
  kstd::list<kernel::KConfigurationRoot::Root> loaded;
  size_t populatedCount = 0;
  uint64_t indexBlockOffset = header.offsetToFirstIndexBlock;

  while (true) {
    for (size_t slot = 0; slot < ENTRIES_PER_INDEX_BLOCK; slot++) {
      ConfigurationIndexEntry entry{};
      const uint64_t entryOffset = indexBlockOffset + offsetof(ConfigurationIndexBlock, entries) + slot * sizeof(ConfigurationIndexEntry);
      kstd::span entryData(reinterpret_cast<uint8_t *>(&entry), sizeof(ConfigurationIndexEntry));
      if (!ReadRawAt(file, entryOffset, entryData)) {
        return false;
      }

      if (entry.keyHash == 0) {
        // Empty slot: reserved space not currently in use.
        continue;
      }

      kernel::KConfigurationRoot::Root loadedEntry{
          .keyHash = entry.keyHash,
          .parentHash = entry.parentHash,
          .valueType = static_cast<kernel::ConfigurationValueType>(entry.valueType),
          .offset = entry.offset,
          .length = entry.length,
          .name = {},
          .indexEntryOffset = static_cast<uint32_t>(entryOffset),
          .indexBlockOffset = static_cast<uint32_t>(indexBlockOffset),
          .nameEntryIndex = entry.nameEntryIndex};
      ReadNameEntry(file, static_cast<uint32_t>(indexBlockOffset), entry.nameEntryIndex, entry.keyHash, loadedEntry.name);
      loaded.emplace_back(loadedEntry);
      populatedCount++;
    }

    uint32_t nextBlockOffset = 0;
    kstd::span nextBlockOffsetData(reinterpret_cast<uint8_t *>(&nextBlockOffset), sizeof(nextBlockOffset));
    if (!ReadRawAt(file, indexBlockOffset + offsetof(ConfigurationIndexBlock, nextBlockOffset), nextBlockOffsetData)) {
      return false;
    }

    if (nextBlockOffset == 0) {
      // Last block in the chain.
      break;
    }

    indexBlockOffset = nextBlockOffset;
  }

  outRoots = kstd::move(loaded);
  outNumRoots = populatedCount;
  return true;
}

}// namespace kernel::configuration_manager_io
