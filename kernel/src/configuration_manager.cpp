
#include "configuration_manager.hpp"
#include "stdkrn.hpp"

#include <mutex.hpp>
#include <optional.hpp>
#include <utility.hpp>

/*
+-----------------------------------------------------------------------+
| HEADER (32 Bytes, Aligned)                                            |
|  - Magic: "CFG1" (4 Bytes)                                            |
|  - Version: uint16_t (2 Bytes)                                        |
|  - Index Entry Count: uint16_t (2 Bytes)                              |
|  - Name Entry Count: uint16_t (2 Bytes)                               |
|  - Reserved / Alignment Padding (6 Bytes)                             |
|  - Offset to First Index Block: uint32_t (4 Bytes, Absolute)          |
|  - Offset to First Name Block: uint32_t (4 Bytes, Absolute)           |
|  - Offset to Data Region: uint32_t (4 Bytes, Absolute)                |
|  - Reserved Field: uint32_t (4 Bytes)                                 |
+-----------------------------------------------------------------------+
| INDEX BLOCK (4096 Bytes Page-Aligned)                                 |
|  - Next Block Offset: uint32_t (4 Bytes, Absolute, 0 = End)           |
|  - Reserved: uint32_t (4 Bytes)                                       |
|  - Entries: Array of 170 x IndexEntry (24 Bytes Each = 4080 Bytes)    |
|    - Path Hash: uint64_t (8 Bytes)                                    |
|    - Name entry index: uint16_t (2 Bytes)                             |
|    - Value Type: uint8_t (1 Byte)                                     |
|    - Reserved/Padding: uint8_t (1 Byte)                               |
|    - Data Offset: uint32_t (4 Bytes, Absolute)                        |
|    - Data Length: uint32_t (4 Bytes)                                  |
+-----------------------------------------------------------------------+
| NAME BLOCK (Page-Aligned)                                             |
|  - Next Block Offset: uint32_t (4 Bytes, Absolute)                    |
|  - Entries: Array of 56 NameEntry (72 bytes each = 4032)              |
|    - Path Hash: uint64_t (8 Bytes)                                    |
|    - Name: char[64]                                                   |
+-----------------------------------------------------------------------+
| DATA REGION                                                           |
|  - Contiguous raw binary blobs and primitive configuration payloads   |
+-----------------------------------------------------------------------+
| INDEX BLOCK (4096 Bytes Page-Aligned)                                 |
|  ...                                                                  |
+-----------------------------------------------------------------------+
| NAME BLOCK (Page-Aligned)                                             |
|  ...                                                                  |
+-----------------------------------------------------------------------+
|  ...                                                                  |
+-----------------------------------------------------------------------+
*/

namespace {
constexpr kstd::array<uint8_t, 4> MAGIC = {'C', 'F', 'G', '1'};

struct ConfigurationKeyTag {
  [[maybe_unused]] static constexpr uint64_t seed = 18366205674763934000ull;
};

// Used for Key hashing
using ConfigurationKey = kernel::StaticIdentifier<ConfigurationKeyTag>;

// Sentinel `nameEntryIndex` meaning "this entry has no associated NAME
// BLOCK entry" - either because a name slot couldn't be allocated for it
// (its paired NAME BLOCK was full) or, for entries predating name support,
// because none was ever persisted.
constexpr uint16_t NO_NAME_ENTRY = 0xFFFF;

struct alignas(8) ConfigurationFileHeader {
  kstd::array<uint8_t, 4> magic{MAGIC};
  uint16_t version{1};
  uint16_t indexEntryCount{0};
  uint16_t nameEntryCount{0};
  uint8_t reserved0[6]{};
  uint32_t offsetToFirstIndexBlock{0};
  uint32_t offsetToFirstNameBlock{0};
  uint32_t offsetToDataRegion{0};
  uint32_t reserved1{0};
};
static_assert(sizeof(ConfigurationFileHeader) == 32, "ConfigurationFileHeader must match the on-disk header size");

// On-disk layout of a single INDEX BLOCK entry (24 bytes, aligned 8).
// Only the index is ever kept in memory (one Root entry per key); the
// DATA REGION payloads are read lazily, on demand, straight from the file.
struct alignas(8) ConfigurationIndexEntry {
  uint64_t keyHash{0};
  uint16_t nameEntryIndex{NO_NAME_ENTRY};
  uint8_t valueType{0};
  uint8_t reserved0{0};
  uint32_t offset{0};
  uint32_t length{0};
};
static_assert(sizeof(ConfigurationIndexEntry) == 24, "ConfigurationIndexEntry must match the on-disk index entry size");

constexpr size_t ENTRIES_PER_INDEX_BLOCK = 170;

// A whole INDEX BLOCK: a fixed-size, page-aligned (4096 byte) chunk holding
// up to 170 entries plus the absolute offset of the next INDEX BLOCK in the
// chain (0 meaning "end of chain").
struct alignas(8) ConfigurationIndexBlock {
  uint32_t nextBlockOffset{0};// absolute file offset, 0 = end of chain
  uint32_t reserved{0};
  ConfigurationIndexEntry entries[ENTRIES_PER_INDEX_BLOCK]{};
  uint8_t padding[8]{};
};
static_assert(sizeof(ConfigurationIndexBlock) == 4096, "ConfigurationIndexBlock must match the on-disk index block size");

constexpr size_t NAME_LENGTH = 64;

// On-disk layout of a single NAME BLOCK entry (72 bytes, aligned 8).
struct alignas(8) ConfigurationNameEntry {
  uint64_t keyHash{0};
  char name[NAME_LENGTH]{};
};
static_assert(sizeof(ConfigurationNameEntry) == 72, "ConfigurationNameEntry must match the on-disk name entry size");

constexpr size_t ENTRIES_PER_NAME_BLOCK = 56;

// A whole NAME BLOCK: a fixed-size, page-aligned (4096 byte) chunk holding
// up to 56 entries plus the absolute offset of the next NAME BLOCK in the
// chain. Each INDEX BLOCK is immediately followed by its paired NAME BLOCK.
struct alignas(8) ConfigurationNameBlock {
  uint32_t nextBlockOffset{0};// absolute file offset, 0 = end of chain
  uint8_t reserved[4]{};
  ConfigurationNameEntry entries[ENTRIES_PER_NAME_BLOCK]{};
  uint8_t padding[56]{};
};
static_assert(sizeof(ConfigurationNameBlock) == 4096, "ConfigurationNameBlock must match the on-disk name block size");

// Byte size of an INDEX BLOCK + its paired NAME BLOCK: the unit that is
// appended, as a whole, every time the chain needs to grow. A block pair's
// DATA REGION starts right after this.
constexpr uint64_t BLOCK_PAIR_BYTES = sizeof(ConfigurationIndexBlock) + sizeof(ConfigurationNameBlock);

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

// The NAME BLOCK immediately follows its paired INDEX BLOCK on disk (see the
// on-disk layout diagram at the top of this file).
uint64_t PairedNameBlockOffset(uint32_t indexBlockOffset) {
  return static_cast<uint64_t>(indexBlockOffset) + sizeof(ConfigurationIndexBlock);
}

// Reads back the short name stored at `nameEntryIndex` within the NAME BLOCK
// paired with `indexBlockOffset`, sanity-checking that its stored keyHash
// still matches. Leaves `outName` untouched (empty) on any failure - a
// missing/mismatched name is not fatal, the entry is simply unnamed.
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

bool EnsureRootIsOpened(const kernel::KObjectPtr<kernel::KConfigurationRoot> &configRoot) {
  if (!configRoot) {
    // Key was misconfigured: doesn't have a valid root
    return false;
  }

  kstd::unique_lock lock(configRoot->mtx);
  if (configRoot->file) {
    // Already opened and indexed by another key sharing the same root
    return true;
  }

  configRoot->file = KE_VFS_OpenFile(configRoot->filePath, kernel::KFileObject::OpenMode::ReadWrite | kernel::KFileObject::OpenMode::Create);
  if (!configRoot->file) {
    // Unable to open file
    return false;
  }

  // Parse the header and put the offset in configRoot
  ConfigurationFileHeader expectedHeader{};
  uint64_t headerBytesRead = 0;

  {
    const kstd::span expectedHeaderData(reinterpret_cast<uint8_t *>(&expectedHeader), sizeof(ConfigurationFileHeader));
    headerBytesRead = KE_VFS_ReadFile(configRoot->file, expectedHeaderData);
  }

  // A brand-new hive's backing storage reads back as a real (but
  // unformatted) block of bytes - either a short/empty read (nothing there
  // yet) or a full read of all-zero bytes (freshly zero-filled storage,
  // e.g. a just-resized disk image) - rather than necessarily a 0-byte
  // read. Either way, an all-zero magic means there is no existing index we
  // could lose, so initialize it in place with a fresh header plus a
  // fully-reserved (but empty) first INDEX BLOCK/NAME BLOCK pair instead of
  // failing: this lets newly registered hives work without any extra
  // provisioning step.
  const bool looksUnformatted = headerBytesRead == 0 ||
                                (headerBytesRead == sizeof(ConfigurationFileHeader) && expectedHeader.magic == kstd::array<uint8_t, 4>{0, 0, 0, 0});

  if (looksUnformatted) {
    const ConfigurationIndexBlock freshIndexBlock{};
    const ConfigurationNameBlock freshNameBlock{};

    const ConfigurationFileHeader freshHeader{
        .offsetToFirstIndexBlock = sizeof(ConfigurationFileHeader),
        .offsetToFirstNameBlock = sizeof(ConfigurationFileHeader) + sizeof(ConfigurationIndexBlock),
        .offsetToDataRegion = sizeof(ConfigurationFileHeader) + static_cast<uint32_t>(BLOCK_PAIR_BYTES)};

    KE_VFS_SeekFile(configRoot->file, 0);
    kstd::span freshHeaderData(reinterpret_cast<const uint8_t *>(&freshHeader), sizeof(ConfigurationFileHeader));
    kstd::span freshIndexBlockData(reinterpret_cast<const uint8_t *>(&freshIndexBlock), sizeof(ConfigurationIndexBlock));
    kstd::span freshNameBlockData(reinterpret_cast<const uint8_t *>(&freshNameBlock), sizeof(ConfigurationNameBlock));
    if (KE_VFS_WriteFile(configRoot->file, freshHeaderData) != sizeof(ConfigurationFileHeader) ||
        KE_VFS_WriteFile(configRoot->file, freshIndexBlockData) != sizeof(ConfigurationIndexBlock) ||
        KE_VFS_WriteFile(configRoot->file, freshNameBlockData) != sizeof(ConfigurationNameBlock)) {
      // Unable to initialize the new hive
      KE_VFS_CloseFile(kstd::move(configRoot->file));
      configRoot->file = nullptr;
      return false;
    }

    configRoot->numRoots = 0;
    configRoot->roots.clear();
    return true;
  }

  if (expectedHeader.magic != MAGIC) {
    // This is a bad file
    KE_VFS_CloseFile(kstd::move(configRoot->file));
    configRoot->file = nullptr;
    return false;
  }

  // Read only the (lightweight) index into memory; the actual value
  // payloads stay on disk and are read on demand when a value is queried.
  // The on-disk index is a linked chain of fixed-size INDEX BLOCKs (170
  // entries each, immediately followed by their paired NAME BLOCK): walk
  // the chain via each block's absolute `nextBlockOffset`, collecting every
  // populated slot (keyHash != 0) until a block's next-block offset is 0.
  configRoot->roots.clear();
  size_t populatedCount = 0;
  uint64_t indexBlockOffset = expectedHeader.offsetToFirstIndexBlock;

  while (true) {
    for (size_t slot = 0; slot < ENTRIES_PER_INDEX_BLOCK; slot++) {
      ConfigurationIndexEntry entry{};
      const uint64_t entryOffset = indexBlockOffset + offsetof(ConfigurationIndexBlock, entries) + slot * sizeof(ConfigurationIndexEntry);
      kstd::span entryData(reinterpret_cast<uint8_t *>(&entry), sizeof(ConfigurationIndexEntry));
      if (!ReadRawAt(configRoot->file, entryOffset, entryData)) {
        KE_VFS_CloseFile(kstd::move(configRoot->file));
        configRoot->file = nullptr;
        configRoot->roots.clear();
        return false;
      }

      if (entry.keyHash == 0) {
        // Empty slot: reserved space not currently in use.
        continue;
      }

      kernel::KConfigurationRoot::Root loadedEntry{
          .keyHash = entry.keyHash,
          .parentHash = 0,
          .valueType = static_cast<kernel::ConfigurationValueType>(entry.valueType),
          .offset = entry.offset,
          .length = entry.length,
          .name = {},
          .indexEntryOffset = static_cast<uint32_t>(entryOffset),
          .indexBlockOffset = static_cast<uint32_t>(indexBlockOffset),
          .nameEntryIndex = entry.nameEntryIndex};
      ReadNameEntry(configRoot->file, static_cast<uint32_t>(indexBlockOffset), entry.nameEntryIndex, entry.keyHash, loadedEntry.name);
      configRoot->roots.emplace_back(loadedEntry);
      populatedCount++;
    }

    uint32_t nextBlockOffset = 0;
    kstd::span nextBlockOffsetData(reinterpret_cast<uint8_t *>(&nextBlockOffset), sizeof(nextBlockOffset));
    if (!ReadRawAt(configRoot->file, indexBlockOffset + offsetof(ConfigurationIndexBlock, nextBlockOffset), nextBlockOffsetData)) {
      KE_VFS_CloseFile(kstd::move(configRoot->file));
      configRoot->file = nullptr;
      configRoot->roots.clear();
      return false;
    }

    if (nextBlockOffset == 0) {
      // Last block in the chain.
      break;
    }

    indexBlockOffset = nextBlockOffset;
  }

  configRoot->numRoots = populatedCount;
  return true;
}

// Drops one reference on `root`, closing the backing file and releasing the
// in-memory index once the last reference goes away. Shared by
// KE_CM_DecrementConfigRootReferenceCount() (releasing a KConfigurationKey's
// reference) and by KE_CM_OpenKey() (releasing the temporary reference it
// holds while searching, on a miss).
void ReleaseConfigRootReference(const kernel::KObjectPtr<kernel::KConfigurationRoot> &root) {
  if (!root) return;

  if (root->configptr_references > 0) {
    root->configptr_references--;
  }

  if (root->configptr_references == 0) {
    // TODO: Save any non commited keys

    if (root->file) {
      KE_VFS_CloseFile(kstd::move(root->file));
      root->file = nullptr;
    }

    root->roots.clear();
    root->numRoots = 0;
  }
}

// Where the next never-before-used byte of the block pair's DATA REGION is:
// one past the highest (offset + length) among the entries belonging to
// that INDEX BLOCK, or right at the start of the block pair's data region
// if it has no entries yet. Each block pair's DATA REGION sits between its
// own fixed-size INDEX BLOCK + NAME BLOCK and the next block pair (or the
// end of the file, for the last pair in the chain), so growth must stay
// scoped to entries sharing the same `indexBlockOffset`.
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

// Finds the first unused (keyHash == 0) slot in the NAME BLOCK paired with
// `indexBlockOffset`, persists `name` into it, and returns its slot index.
// Unlike FindOrAllocateFreeSlot() for the INDEX BLOCK chain, this never
// allocates a brand-new NAME BLOCK: a paired NAME BLOCK only has room for 56
// names even though its INDEX BLOCK has room for 170 entries, so it is
// possible (if unlikely in practice) for this to simply run out of room. In
// that case the caller's entry is left unnamed rather than failing outright.
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

// Finds the value entry for `keyHash`, validates its stored type/size
// against what the caller expects, and reads its raw payload bytes.
bool ReadKeyValue(const kernel::KConfigurationRoot &root, uint64_t keyHash, kernel::ConfigurationValueType expectedType, kstd::span<uint8_t> outValue) {
  for (const auto &entry: root.roots) {
    if (entry.keyHash == keyHash) {
      if (entry.valueType != expectedType || entry.length != outValue.size()) {
        return false;
      }
      return ReadRawAt(root.file, entry.offset, outValue);
    }
  }
  return false;
}

// Finds the first unused (keyHash == 0) INDEX BLOCK slot across the hive's
// chained block pairs, extending the chain with a brand-new (empty)
// INDEX BLOCK/NAME BLOCK pair at the end of the file if every existing
// INDEX BLOCK is already full. Slots within a block are only ever appended
// in order (deletion of entries - and therefore reuse of interior holes -
// isn't implemented yet), so the first free slot in a block is simply the
// one right after however many entries already live there.
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

// Finds the existing value entry for `keyHash`, writes (or relocates, if the
// payload size changed) its stored bytes, and persists both the value data
// and its (possibly-updated) index entry to disk. Does not create new
// entries: the target key must already have an entry in the index (i.e. it
// must have been found by KE_CM_OpenKey() / created by KE_CM_CreateKey()).
bool WriteKeyValue(kernel::KConfigurationRoot &root, uint64_t keyHash, kernel::ConfigurationValueType type, kstd::span<const uint8_t> data) {
  for (auto &entry: root.roots) {
    if (entry.keyHash == keyHash) {
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

template<typename T>
bool GetFixedValue(const kernel::KConfigurationKeyPtr &key, kernel::ConfigurationValueType type, T &outValue) {
  if (!key || !key->configRoot) return false;

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!key->configRoot->file) return false;

  T value{};
  if (!ReadKeyValue(*key->configRoot, key->keyHash, type, kstd::span(reinterpret_cast<uint8_t *>(&value), sizeof(T)))) {
    return false;
  }
  outValue = value;
  return true;
}

template<typename T>
bool SetFixedValue(const kernel::KConfigurationKeyPtr &key, kernel::ConfigurationValueType type, const T &value) {
  if (!key || !key->configRoot) return false;

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!key->configRoot->file) return false;

  return WriteKeyValue(*key->configRoot, key->keyHash, type, kstd::span(reinterpret_cast<const uint8_t *>(&value), sizeof(T)));
}
}// namespace

UNDOS_KERNEL_API_DEF bool KE_CM_IncrementConfigRootReferenceCount(const kernel::KConfigurationKey *key) noexcept {
  if (!key || !key->configRoot) return false;

  // Increment references on the underlying root object
  key->configRoot->configptr_references++;
  return true;
}

UNDOS_KERNEL_API_DEF bool KE_CM_DecrementConfigRootReferenceCount(const kernel::KConfigurationKey *key) noexcept {
  if (!key || !key->configRoot) return false;

  ReleaseConfigRootReference(key->configRoot);
  return true;
}

UNDOS_KERNEL_API_DEF kernel::KConfigurationKeyPtr KE_CM_OpenRoot(const kstd::string_view &rootName) noexcept {
  if (const auto hiveRoot = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigRoot(), rootName)) {
    if (!EnsureRootIsOpened(hiveRoot)) {
      return nullptr;
    }

    auto obj = KE_CreateKPtrObject<kernel::KConfigurationKey>();
    if (!obj) {
      return nullptr;
    }

    // configRoot must be assigned before bumping the reference count: the
    // count lives on the shared root, not on this handle, and
    // KE_CM_IncrementConfigRootReferenceCount() looks it up through it.
    obj->configRoot = hiveRoot;
    obj->keyHash = 0;// The root itself has no path component to hash
    obj->access = kernel::KConfigurationKeyAccess::Key;
    KE_CM_IncrementConfigRootReferenceCount(obj.get());
    return obj;
  }
  return nullptr;
}

UNDOS_KERNEL_API_DEF kernel::KConfigurationKeyPtr KE_CM_OpenKey(const kernel::KConfigurationKeyPtr &parentKey,
                                                                const kstd::string_view &subKeyName,
                                                                kernel::KConfigurationKeyAccess desiredAccess) noexcept {
  // if parentKey is null then subKeyName must be a fully qualified name, starting with the root name
  kernel::KObjectPtr<kernel::KConfigurationRoot> root = nullptr;
  ConfigurationKey realKeyHash;

  if (parentKey != nullptr) {
    root = parentKey->configRoot;
    realKeyHash = ConfigurationKey::from_string(subKeyName);
  } else {
    // Find the first `/` and treat anything before as root
    const auto firstSlash = subKeyName.find('/');
    if (firstSlash == kstd::string_view::npos) {
      return nullptr;
    }
    realKeyHash = ConfigurationKey::from_string(subKeyName.substr(firstSlash + 1));
    root = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigRoot(), subKeyName.substr(0, firstSlash));
  }

  if (!EnsureRootIsOpened(root)) {
    // Cannot open root
    return nullptr;
  }

  // EnsureRootIsOpened() may have just performed the lazy open above: hold a
  // temporary reference for the duration of the search so a miss below
  // releases it again instead of leaking an open file with nothing
  // referencing it.
  root->configptr_references++;

  kernel::KConfigurationKeyPtr found = nullptr;

  // Find realKeyHash in root
  for (const auto &child: root->roots) {
    if (child.keyHash == realKeyHash) {
      if (auto obj = KE_CreateKPtrObject<kernel::KConfigurationKey>()) {
        // Same ordering requirement as KE_CM_OpenRoot: assign configRoot
        // first so the reference-count bump below can find it.
        obj->configRoot = root;
        obj->keyHash = child.keyHash;
        obj->access = desiredAccess;
        KE_CM_IncrementConfigRootReferenceCount(obj.get());
        found = obj;
      }
      break;
    }
  }

  ReleaseConfigRootReference(root);
  return found;
}

UNDOS_KERNEL_API_DEF kernel::KConfigurationKeyPtr KE_CM_CreateKey(const kernel::KConfigurationKeyPtr &parentKey,
                                                                  const kstd::string_view &subKeyName,
                                                                  kernel::KConfigurationKeyAccess desiredAccess) noexcept {
  // Same parent/root resolution as KE_CM_OpenKey(): if parentKey is null,
  // subKeyName must be fully qualified (starting with the root/hive name).
  kernel::KObjectPtr<kernel::KConfigurationRoot> root = nullptr;
  ConfigurationKey realKeyHash;
  kstd::string_view resolvedName = subKeyName;// the short name actually hashed into realKeyHash

  if (parentKey != nullptr) {
    root = parentKey->configRoot;
    realKeyHash = ConfigurationKey::from_string(subKeyName);
  } else {
    // Find the first `/` and treat anything before as root
    const auto firstSlash = subKeyName.find('/');
    if (firstSlash == kstd::string_view::npos) {
      return nullptr;
    }
    resolvedName = subKeyName.substr(firstSlash + 1);
    realKeyHash = ConfigurationKey::from_string(resolvedName);
    root = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigRoot(), subKeyName.substr(0, firstSlash));
  }

  if (!EnsureRootIsOpened(root)) {
    // Cannot open root
    return nullptr;
  }

  // Hold a temporary reference for the duration of this call, mirroring
  // KE_CM_OpenKey(): EnsureRootIsOpened() may have just performed the lazy
  // open above, and every path below needs to release this again before
  // returning.
  root->configptr_references++;

  kernel::KConfigurationKeyPtr result = nullptr;

  {
    kstd::unique_lock lock(root->mtx);

    bool existsAlready = false;
    for (const auto &child: root->roots) {
      if (child.keyHash == realKeyHash) {
        existsAlready = true;
        break;
      }
    }

    if (existsAlready && (desiredAccess & kernel::KConfigurationKeyAccess::Exclusive)) {
      // Caller demanded that the key not already exist
      result = nullptr;
    } else if (existsAlready) {
      // Key already exists and the caller didn't require exclusivity:
      // behave like KE_CM_OpenKey() and just hand back a handle to it.
      if (auto obj = KE_CreateKPtrObject<kernel::KConfigurationKey>()) {
        obj->configRoot = root;
        obj->keyHash = realKeyHash;
        obj->access = desiredAccess;
        KE_CM_IncrementConfigRootReferenceCount(obj.get());
        result = obj;
      }
    } else {
      uint32_t indexBlockOffset = 0;
      uint32_t indexEntryOffset = 0;
      if (FindOrAllocateFreeSlot(*root, indexBlockOffset, indexEntryOffset)) {
        // Persist a name entry alongside the index entry so KE_CM_EnumKey()/
        // KE_CM_EnumValue() can later report this key's short name. A
        // paired NAME BLOCK has fewer slots than its INDEX BLOCK (56 vs
        // 170), so this can fail even though the index slot was found: the
        // key is still created in that case, it is simply left unnamed.
        uint16_t nameEntryIndex = NO_NAME_ENTRY;
        FindOrAllocateNameSlot(root->file, indexBlockOffset, realKeyHash, resolvedName, nameEntryIndex);

        // A freshly created key has no value yet: it starts out as a
        // folder-like entry (Invalid type, zero-length payload) until
        // KE_CM_SetKeyValue*() gives it a value.
        const kernel::KConfigurationRoot::Root newEntry{
            .keyHash = realKeyHash,
            .parentHash = parentKey ? parentKey->keyHash : 0,
            .valueType = kernel::ConfigurationValueType::Invalid,
            .offset = 0,
            .length = 0,
            .name = resolvedName,
            .indexEntryOffset = indexEntryOffset,
            .indexBlockOffset = indexBlockOffset,
            .nameEntryIndex = nameEntryIndex};

        if (PersistIndexEntry(root->file, newEntry)) {
          root->roots.emplace_back(newEntry);
          root->numRoots++;

          if (auto obj = KE_CreateKPtrObject<kernel::KConfigurationKey>()) {
            obj->configRoot = root;
            obj->keyHash = newEntry.keyHash;
            obj->access = desiredAccess;
            KE_CM_IncrementConfigRootReferenceCount(obj.get());
            result = obj;
          }
        }
      }
    }
  }

  ReleaseConfigRootReference(root);
  return result;
}

UNDOS_KERNEL_API_DEF bool KE_CM_DeleteKey(const kernel::KConfigurationKeyPtr &parentKey,
                                          const kstd::string_view &subKeyName) noexcept {
  // Same parent/root resolution as KE_CM_OpenKey()/KE_CM_CreateKey(): if
  // parentKey is null, subKeyName must be fully qualified (starting with
  // the root/hive name).
  kernel::KObjectPtr<kernel::KConfigurationRoot> root = nullptr;
  ConfigurationKey realKeyHash;

  if (parentKey != nullptr) {
    root = parentKey->configRoot;
    realKeyHash = ConfigurationKey::from_string(subKeyName);
  } else {
    // Find the first `/` and treat anything before as root
    const auto firstSlash = subKeyName.find('/');
    if (firstSlash == kstd::string_view::npos) {
      return false;
    }
    realKeyHash = ConfigurationKey::from_string(subKeyName.substr(firstSlash + 1));
    root = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigRoot(), subKeyName.substr(0, firstSlash));
  }

  if (!EnsureRootIsOpened(root)) {
    // Cannot open root
    return false;
  }

  // Hold a temporary reference for the duration of this call, mirroring
  // KE_CM_OpenKey()/KE_CM_CreateKey(): EnsureRootIsOpened() may have just
  // performed the lazy open above, and every path below needs to release
  // this again before returning.
  root->configptr_references++;

  bool deleted = false;

  {
    kstd::unique_lock lock(root->mtx);

    for (auto it = root->roots.begin(); it != root->roots.end(); ++it) {
      if (it->keyHash == realKeyHash) {
        // Zero out the on-disk index entry so the slot reads back as
        // empty (keyHash == 0) the next time the hive is opened. The slot
        // itself is not reclaimed for reuse by FindOrAllocateFreeSlot()
        // until the hive is reloaded from disk (deletion doesn't rewrite
        // the whole index), so it will simply stay unused until then.
        kernel::KConfigurationRoot::Root cleared{};
        cleared.indexEntryOffset = it->indexEntryOffset;
        if (PersistIndexEntry(root->file, cleared)) {
          // Also free up the paired name slot (if any) so
          // FindOrAllocateNameSlot() can reuse it for a future key.
          if (it->nameEntryIndex != NO_NAME_ENTRY) {
            PersistNameEntry(root->file, it->indexBlockOffset, it->nameEntryIndex, 0, kstd::string_view());
          }

          root->roots.erase(it);
          if (root->numRoots > 0) {
            root->numRoots--;
          }
          deleted = true;
        }
        break;
      }
    }
  }

  ReleaseConfigRootReference(root);
  return deleted;
}

UNDOS_KERNEL_API_DEF bool KE_CM_FlushKey(const kernel::KConfigurationKeyPtr &keyHandle) noexcept {
  (void) keyHandle;
  return false;
}

UNDOS_KERNEL_API_DEF bool KM_CM_GetKeyInfo(const kernel::KConfigurationKeyPtr &keyHandle,
                                           kernel::KConfigurationKeyInfo &outInfo) noexcept {
  if (!keyHandle || !keyHandle->configRoot) return false;

  const auto &root = keyHandle->configRoot;
  kstd::unique_lock lock(root->mtx);
  if (!root->file) return false;

  if (keyHandle->keyHash == 0) {
    // The hive root itself: it has no matching index entry (it isn't a
    // regular key), but it always exists and is always folder-like.
    outInfo.lastModified = kstd::chrono::system_time{};
    outInfo.name.clear();
    outInfo.isFolder = true;
    outInfo.typeIfNotFolder = kernel::ConfigurationValueType::Invalid;
    return true;
  }

  for (const auto &entry: root->roots) {
    if (entry.keyHash == keyHandle->keyHash) {
      // No on-disk modification-time tracking exists yet: default-initialize
      // rather than fabricate a value.
      outInfo.lastModified = kstd::chrono::system_time{};
      outInfo.name = entry.name;
      outInfo.isFolder = entry.valueType == kernel::ConfigurationValueType::Invalid;
      outInfo.typeIfNotFolder = entry.valueType;
      return true;
    }
  }
  return false;
}

// region Configuration Get Value
UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueBool(const kernel::KConfigurationKeyPtr &key, bool &outValue) noexcept {
  uint8_t raw = 0;
  if (!GetFixedValue(key, kernel::ConfigurationValueType::Boolean, raw)) {
    return false;
  }
  outValue = raw != 0;
  return true;
}

UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueUInt32(const kernel::KConfigurationKeyPtr &key, uint32_t &outValue) noexcept {
  return GetFixedValue(key, kernel::ConfigurationValueType::UInt32, outValue);
}

UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueUInt64(const kernel::KConfigurationKeyPtr &key, uint64_t &outValue) noexcept {
  return GetFixedValue(key, kernel::ConfigurationValueType::UInt64, outValue);
}

UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueSInt32(const kernel::KConfigurationKeyPtr &key, int32_t &outValue) noexcept {
  return GetFixedValue(key, kernel::ConfigurationValueType::SInt32, outValue);
}

UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueSInt64(const kernel::KConfigurationKeyPtr &key, int64_t &outValue) noexcept {
  return GetFixedValue(key, kernel::ConfigurationValueType::SInt64, outValue);
}

UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueString(const kernel::KConfigurationKeyPtr &key, kstd::string_view &outValue) noexcept {
  if (!key || !key->configRoot) return false;

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!key->configRoot->file) return false;

  for (const auto &entry: key->configRoot->roots) {
    if (entry.keyHash == key->keyHash) {
      if (entry.valueType != kernel::ConfigurationValueType::String) {
        return false;
      }
      if (entry.length > decltype(key->stringValueScratch)::capacity()) {
        // Too large to fit the scratch buffer backing the returned view
        return false;
      }

      char raw[decltype(key->stringValueScratch)::capacity()]{};
      if (!ReadRawAt(key->configRoot->file, entry.offset, kstd::span(reinterpret_cast<uint8_t *>(raw), entry.length))) {
        return false;
      }

      key->stringValueScratch = kstd::string_view(raw, entry.length);
      outValue = key->stringValueScratch;
      return true;
    }
  }
  return false;
}
// endregion

// region Configuration Set Value
UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueBool(const kernel::KConfigurationKeyPtr &key, bool outValue) noexcept {
  const uint8_t raw = outValue ? 1 : 0;
  return SetFixedValue(key, kernel::ConfigurationValueType::Boolean, raw);
}

UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueUInt32(const kernel::KConfigurationKeyPtr &key, uint32_t outValue) noexcept {
  return SetFixedValue(key, kernel::ConfigurationValueType::UInt32, outValue);
}

UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueUInt64(const kernel::KConfigurationKeyPtr &key, uint64_t outValue) noexcept {
  return SetFixedValue(key, kernel::ConfigurationValueType::UInt64, outValue);
}

UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueSInt32(const kernel::KConfigurationKeyPtr &key, int32_t outValue) noexcept {
  return SetFixedValue(key, kernel::ConfigurationValueType::SInt32, outValue);
}

UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueSInt64(const kernel::KConfigurationKeyPtr &key, int64_t outValue) noexcept {
  return SetFixedValue(key, kernel::ConfigurationValueType::SInt64, outValue);
}

UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueString(const kernel::KConfigurationKeyPtr &key, const kstd::string_view &outValue) noexcept {
  if (!key || !key->configRoot) return false;

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!key->configRoot->file) return false;

  return WriteKeyValue(*key->configRoot, key->keyHash, kernel::ConfigurationValueType::String,
                       kstd::span(reinterpret_cast<const uint8_t *>(outValue.data()), outValue.size()));
}
// endregion

UNDOS_KERNEL_API_DEF bool KE_CM_DeleteValue(const kernel::KConfigurationKeyPtr &keyHandle, kstd::string_view valueName) noexcept {
  if (!keyHandle || !keyHandle->configRoot) return false;

  // Same hash resolution as KE_CM_OpenKey()/KE_CM_CreateKey() when a
  // parentKey is given: valueName is hashed on its own, keyHandle is only
  // used to reach the right hive.
  const ConfigurationKey realValueHash = ConfigurationKey::from_string(valueName);
  const auto &root = keyHandle->configRoot;

  kstd::unique_lock lock(root->mtx);
  if (!root->file) return false;

  for (auto &entry: root->roots) {
    if (entry.keyHash == realValueHash) {
      if (entry.valueType == kernel::ConfigurationValueType::Invalid) {
        // No value currently set on this entry: nothing to delete.
        return false;
      }

      // Unlike KE_CM_DeleteKey(), the index entry itself is kept (it turns
      // back into a folder-like, valueless entry, same as one freshly
      // created by KE_CM_CreateKey()) - only its value is cleared.
      entry.valueType = kernel::ConfigurationValueType::Invalid;
      entry.offset = 0;
      entry.length = 0;
      return PersistIndexEntry(root->file, entry);
    }
  }
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_EnumKey(const kernel::KConfigurationKeyPtr &keyHandle, uint32_t index, kstd::string_view &outName) noexcept {
  if (!keyHandle || !keyHandle->configRoot) return false;

  const auto &root = keyHandle->configRoot;
  kstd::unique_lock lock(root->mtx);
  if (!root->file) return false;

  // Enumerate only the folder-like (Invalid-typed) entries that are direct
  // children of keyHandle, in the same stable order they appear in
  // root->roots (the order they were loaded from / appended to the index).
  uint32_t matched = 0;
  for (const auto &entry: root->roots) {
    if (entry.parentHash != keyHandle->keyHash) continue;
    if (entry.valueType != kernel::ConfigurationValueType::Invalid) continue;

    if (matched == index) {
      keyHandle->enumNameScratch = entry.name;
      outName = keyHandle->enumNameScratch;
      return true;
    }
    matched++;
  }
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_EnumValue(const kernel::KConfigurationKeyPtr &keyHandle, uint32_t index, kstd::string_view &outName, kernel::ConfigurationValueType &outType) noexcept {
  if (!keyHandle || !keyHandle->configRoot) return false;

  const auto &root = keyHandle->configRoot;
  kstd::unique_lock lock(root->mtx);
  if (!root->file) return false;

  // Same as KE_CM_EnumKey(), but for the value-holding (non-Invalid-typed)
  // direct children of keyHandle instead of the folder-like ones.
  uint32_t matched = 0;
  for (const auto &entry: root->roots) {
    if (entry.parentHash != keyHandle->keyHash) continue;
    if (entry.valueType == kernel::ConfigurationValueType::Invalid) continue;

    if (matched == index) {
      keyHandle->enumNameScratch = entry.name;
      outName = keyHandle->enumNameScratch;
      outType = entry.valueType;
      return true;
    }
    matched++;
  }
  return false;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KConfigurationRoot> KE_CM_RegisterHive(const kstd::string_view &hiveName, const kstd::string_view &filePath) noexcept {
  if (KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigRoot(), hiveName)) {
    // A hive with that name is already registered
    return nullptr;
  }

  auto hive = kernel::CreateKObject<kernel::KConfigurationRoot>(hiveName);
  if (!hive) {
    return nullptr;
  }
  hive->filePath = filePath;

  if (!KE_OB_InsertObject(KE_OB_GetConfigRoot(), hive)) {
    return nullptr;
  }

  // The backing file itself is created/formatted lazily, the first time the
  // hive is opened (see EnsureRootIsOpened), so a freshly registered hive
  // doesn't need its file to exist yet.
  return hive;
}

namespace kernel::configuration_manager {
void init() {
  // Load known hives into the object manager
  KE_CM_RegisterHive("LocalMachine", "/System/Config/LocalMachine");
}
}// namespace kernel::configuration_manager
