
#include "configuration_manager.hpp"
#include "stdkrn.hpp"

#include <mutex.hpp>
#include <optional.hpp>
#include <utility.hpp>

/*
+-----------------------------------------------------------------------+
| HEADER (8 Bytes)                                                      |
|  - Magic: "CFG1" (4 Bytes)                                            |
|  - Version: uint16_t (2 Bytes, Little-Endian)                         |
|  - Key Count: uint16_t (2 Bytes, Little-Endian)                       |
+-----------------------------------------------------------------------+
| INDEX TABLE (N Entries x 24 Bytes Each - aligned 8)                   |
| Index tables always have space for 170 entries (4080 bytes)           |
|  - Path Hash: uint64_t (8 Bytes, ObjectType value) 0=empty            |
|  - Value Type: uint8_t  (1 Byte, ConfigurationValueType)              |
|  - Data Offset: uint32_t (4 Bytes, relative to start of file)         |
|  - Data Length: uint16_t (2 Bytes, byte count of payload)             |
+-----------------------------------------------------------------------+
| Pointer to next group of index entries (16 bytes)                     |
|  - Next Index Offset: uint32_t (4 Bytes, relative to this point)      |
+-----------------------------------------------------------------------+
| DATA REGION                                                           |
|  - Raw Payloads (Little-Endian encoded primitive values or bytes)     |
+-----------------------------------------------------------------------+
| INDEX TABLE (N Entries x 24 Bytes Each - aligned 8)                   |
| ...                                                                   |
+-----------------------------------------------------------------------+
| Pointer to next group of index entries (16 bytes)                     |
|  - Next Index Offset: uint32_t (4 Bytes, relative to this point)      |
+-----------------------------------------------------------------------+
| DATA REGION                                                           |
|  - Raw Payloads (Little-Endian encoded primitive values or bytes)     |
+-----------------------------------------------------------------------+
*/

namespace {
constexpr kstd::array<uint8_t, 4> MAGIC = {'C', 'F', 'G', '1'};

struct ConfigurationKeyTag {
  [[maybe_unused]] static constexpr uint64_t seed = 18366205674763934000ull;
};

// Used for Key hashing
using ConfigurationKey = kernel::StaticIdentifier<ConfigurationKeyTag>;

struct alignas(8) ConfigurationFileHeader {
  kstd::array<uint8_t, 4> magic{MAGIC};
  uint16_t version{1};
  uint16_t keyCount{0};
};

// On-disk layout of a single INDEX TABLE entry (24 bytes, aligned 8).
// Only the index table is ever kept in memory (one Root entry per key); the
// DATA REGION payloads are read lazily, on demand, straight from the file.
struct alignas(8) ConfigurationIndexEntry {
  uint64_t keyHash{0};
  uint8_t valueType{0};
  uint8_t reserved0[3]{};
  uint32_t offset{0};
  uint16_t length{0};
  uint8_t reserved1[6]{};
};
static_assert(sizeof(ConfigurationIndexEntry) == 24, "ConfigurationIndexEntry must match the on-disk index entry size");

struct alignas(8) ConfigurationIndexNextGroup {
  uint32_t nextIndexOffset{0};
  uint8_t reserved[12]{};
};
static_assert(sizeof(ConfigurationIndexNextGroup) == 16, "ConfigurationIndexGroup must match the on-disk index group size");

struct alignas(8) ConfigurationIndexGroup {
  ConfigurationIndexEntry entries[170];
  ConfigurationIndexNextGroup nextGroup;
};
static_assert(sizeof(ConfigurationIndexGroup) == 4096, "ConfigurationIndexGroupHeader must match the on-disk index group header size");

constexpr size_t ENTRIES_PER_GROUP = 170;
// Byte size of the fixed-size (170-entry) INDEX TABLE portion of a group, not
// including the 16-byte next-group pointer that immediately follows it.
constexpr uint64_t INDEX_TABLE_BYTES = ENTRIES_PER_GROUP * sizeof(ConfigurationIndexEntry);
// Byte size of a whole group: its INDEX TABLE plus the next-group pointer
// that follows it. The group's DATA REGION starts right after this.
constexpr uint64_t GROUP_BYTES = sizeof(ConfigurationIndexGroup);
static_assert(GROUP_BYTES == INDEX_TABLE_BYTES + sizeof(ConfigurationIndexNextGroup));

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
    kstd::span expectedHeaderData(reinterpret_cast<uint8_t *>(&expectedHeader), sizeof(ConfigurationFileHeader));
    headerBytesRead = KE_VFS_ReadFile(configRoot->file, expectedHeaderData);
  }

  if (headerBytesRead == 0) {
    // Either a brand-new hive (the backing file was just created by the
    // KFileObject::OpenMode::Create above and has no header yet) or one
    // that hasn't been formatted. Either way, there is no existing index we
    // could lose, so initialize it in place with a fresh header plus a
    // fully-reserved (but empty) first INDEX TABLE group instead of failing:
    // this lets newly registered hives work without any extra provisioning
    // step, and matches the on-disk format's requirement that an index
    // table always reserves space for 170 entries plus its next-group
    // pointer, even while empty.
    const ConfigurationFileHeader freshHeader{};
    const ConfigurationIndexGroup freshGroup{};

    KE_VFS_SeekFile(configRoot->file, 0);
    kstd::span freshHeaderData(reinterpret_cast<const uint8_t *>(&freshHeader), sizeof(ConfigurationFileHeader));
    kstd::span freshGroupData(reinterpret_cast<const uint8_t *>(&freshGroup), sizeof(ConfigurationIndexGroup));
    if (KE_VFS_WriteFile(configRoot->file, freshHeaderData) != sizeof(ConfigurationFileHeader) ||
        KE_VFS_WriteFile(configRoot->file, freshGroupData) != sizeof(ConfigurationIndexGroup)) {
      // Unable to initialize the new hive
      KE_VFS_CloseFile(kstd::move(configRoot->file));
      configRoot->file = nullptr;
      return false;
    }

    configRoot->numRoots = 0;
    configRoot->roots.clear();
    return true;
  }

  // Read only the (lightweight) index table into memory; the actual value
  // payloads stay on disk and are read on demand when a value is queried.
  // The on-disk index is a linked chain of fixed-size groups (170 entries +
  // a next-group pointer each): walk the chain, collecting every populated
  // slot (keyHash != 0) until a group's next-group pointer is 0.
  configRoot->roots.clear();
  size_t populatedCount = 0;
  uint64_t groupOffset = sizeof(ConfigurationFileHeader);

  while (true) {
    for (size_t slot = 0; slot < ENTRIES_PER_GROUP; slot++) {
      ConfigurationIndexEntry entry{};
      const uint64_t entryOffset = groupOffset + slot * sizeof(ConfigurationIndexEntry);
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

      configRoot->roots.emplace_back(kernel::KConfigurationRoot::Root{
          .keyHash = entry.keyHash,
          .parentHash = 0,
          .valueType = static_cast<kernel::ConfigurationValueType>(entry.valueType),
          .offset = entry.offset,
          .length = entry.length,
          .name = {},
          .indexEntryOffset = static_cast<uint32_t>(entryOffset),
          .groupOffset = static_cast<uint32_t>(groupOffset)});
      populatedCount++;
    }

    ConfigurationIndexNextGroup nextGroup{};
    kstd::span nextGroupData(reinterpret_cast<uint8_t *>(&nextGroup), sizeof(ConfigurationIndexNextGroup));
    if (!ReadRawAt(configRoot->file, groupOffset + INDEX_TABLE_BYTES, nextGroupData)) {
      KE_VFS_CloseFile(kstd::move(configRoot->file));
      configRoot->file = nullptr;
      configRoot->roots.clear();
      return false;
    }

    if (nextGroup.nextIndexOffset == 0) {
      // Last group in the chain.
      break;
    }

    groupOffset = (groupOffset + INDEX_TABLE_BYTES) + nextGroup.nextIndexOffset;
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

// Where the next never-before-used byte of the group's DATA REGION is: one
// past the highest (offset + length) among the entries belonging to that
// group, or right at the start of the group's data region if it has no
// entries yet. Each group's DATA REGION sits between its own fixed-size
// index table (and next-group pointer) and the next group's index table (or
// the end of the file, for the last group in the chain), so growth must
// stay scoped to entries sharing the same `groupOffset`.
uint64_t ComputeDataRegionEnd(const kernel::KConfigurationRoot &root, uint32_t groupOffset) {
  uint64_t end = groupOffset + GROUP_BYTES;
  for (const auto &entry: root.roots) {
    if (entry.groupOffset != groupOffset) continue;
    const uint64_t entryEnd = static_cast<uint64_t>(entry.offset) + entry.length;
    if (entryEnd > end) end = entryEnd;
  }
  return end;
}

bool PersistIndexEntry(const kernel::KObjectPtr<kernel::KFileObject> &file, const kernel::KConfigurationRoot::Root &root) {
  const ConfigurationIndexEntry onDisk{
      .keyHash = root.keyHash,
      .valueType = static_cast<uint8_t>(root.valueType),
      .reserved0 = {},
      .offset = root.offset,
      .length = root.length,
      .reserved1 = {}};
  return WriteRawAt(file, root.indexEntryOffset,
                    kstd::span(reinterpret_cast<const uint8_t *>(&onDisk), sizeof(ConfigurationIndexEntry)));
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

// Finds the first unused (keyHash == 0) index-table slot across the hive's
// chained groups, extending the chain with a brand-new (empty) group at the
// end of the file if every existing group is already full. Slots within a
// group are only ever appended in order (deletion of entries - and
// therefore reuse of interior holes - isn't implemented yet), so the first
// free slot in a group is simply the one right after however many entries
// already live there.
bool FindOrAllocateFreeSlot(kernel::KConfigurationRoot &root, uint32_t &outGroupOffset, uint32_t &outIndexEntryOffset) {
  uint64_t groupOffset = sizeof(ConfigurationFileHeader);

  while (true) {
    size_t occupied = 0;
    for (const auto &entry: root.roots) {
      if (entry.groupOffset == groupOffset) occupied++;
    }

    if (occupied < ENTRIES_PER_GROUP) {
      outGroupOffset = static_cast<uint32_t>(groupOffset);
      outIndexEntryOffset = static_cast<uint32_t>(groupOffset + occupied * sizeof(ConfigurationIndexEntry));
      return true;
    }

    ConfigurationIndexNextGroup nextGroup{};
    kstd::span nextGroupData(reinterpret_cast<uint8_t *>(&nextGroup), sizeof(ConfigurationIndexNextGroup));
    if (!ReadRawAt(root.file, groupOffset + INDEX_TABLE_BYTES, nextGroupData)) {
      return false;
    }

    if (nextGroup.nextIndexOffset != 0) {
      groupOffset = (groupOffset + INDEX_TABLE_BYTES) + nextGroup.nextIndexOffset;
      continue;
    }

    // This is the last group in the chain and it's full: append a
    // brand-new (empty) group right after this group's data region, and
    // link the two together.
    const uint64_t newGroupOffset = ComputeDataRegionEnd(root, static_cast<uint32_t>(groupOffset));
    const ConfigurationIndexGroup freshGroup{};
    if (!WriteRawAt(root.file, newGroupOffset,
                    kstd::span(reinterpret_cast<const uint8_t *>(&freshGroup), sizeof(ConfigurationIndexGroup)))) {
      return false;
    }

    const ConfigurationIndexNextGroup linkToNewGroup{
        .nextIndexOffset = static_cast<uint32_t>(newGroupOffset - (groupOffset + INDEX_TABLE_BYTES)),
        .reserved = {}};
    if (!WriteRawAt(root.file, groupOffset + INDEX_TABLE_BYTES,
                    kstd::span(reinterpret_cast<const uint8_t *>(&linkToNewGroup), sizeof(ConfigurationIndexNextGroup)))) {
      return false;
    }

    groupOffset = newGroupOffset;
  }
}

// Finds the existing value entry for `keyHash`, writes (or relocates, if the
// payload size changed) its stored bytes, and persists both the value data
// and its (possibly-updated) index entry to disk. Does not create new
// entries: the target key must already have an entry in the index (i.e. it
// must have been found by KE_CM_OpenKey() / created by KE_CM_CreateKey()).
bool WriteKeyValue(kernel::KConfigurationRoot &root, uint64_t keyHash, kernel::ConfigurationValueType type, kstd::span<const uint8_t> data) {
  if (data.size() > UINT16_MAX) {
    // Payload wouldn't fit in the 16-bit on-disk length field
    return false;
  }

  for (auto &entry: root.roots) {
    if (entry.keyHash == keyHash) {
      uint64_t targetOffset = entry.offset;
      if (data.size() > entry.length) {
        // Doesn't fit in the space it previously used: relocate to the end
        // of this entry's own group's data region instead of overwriting
        // (the old bytes are left behind, unreachable, until the hive is
        // compacted).
        targetOffset = ComputeDataRegionEnd(root, entry.groupOffset);
      }

      if (!WriteRawAt(root.file, targetOffset, data)) {
        return false;
      }

      entry.offset = static_cast<uint32_t>(targetOffset);
      entry.length = static_cast<uint16_t>(data.size());
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
      uint32_t groupOffset = 0;
      uint32_t indexEntryOffset = 0;
      if (FindOrAllocateFreeSlot(*root, groupOffset, indexEntryOffset)) {
        // A freshly created key has no value yet: it starts out as a
        // folder-like entry (Invalid type, zero-length payload) until
        // KE_CM_SetKeyValue*() gives it a value.
        const kernel::KConfigurationRoot::Root newEntry{
            .keyHash = realKeyHash,
            .parentHash = 0,
            .valueType = kernel::ConfigurationValueType::Invalid,
            .offset = 0,
            .length = 0,
            .name = {},
            .indexEntryOffset = indexEntryOffset,
            .groupOffset = groupOffset};

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
  (void) keyHandle;
  (void) outInfo;
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
      if (entry.length > key->stringValueScratch.capacity()) {
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
  (void) keyHandle;
  (void) index;
  (void) outName;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_EnumValue(const kernel::KConfigurationKeyPtr &keyHandle, uint32_t index, kstd::string_view &outName, kernel::ConfigurationValueType &outType) noexcept {
  (void) keyHandle;
  (void) index;
  (void) outName;
  (void) outType;
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
