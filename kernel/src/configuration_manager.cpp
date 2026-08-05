
#include "configuration_manager.hpp"
#include "configuration_manager_io.hpp"
#include "stdkrn.hpp"

#include <mutex.hpp>
#include <optional.hpp>
#include <utility.hpp>

// The on-disk hive file format (the HEADER/INDEX BLOCK/NAME BLOCK/DATA
// REGION layout) and every byte-level read/write against it live in
// configuration_manager_io.hpp/.cpp: this file only ever talks to a hive's
// backing file through that module's functions (imported below via a
// using-directive), never touching file offsets or on-disk structures
// directly.
using namespace kernel::configuration_manager_io;

namespace {
constexpr kstd::string_view kConfigurationLocalMachine = "/System/Config/MACHI";
constexpr kstd::string_view kConfigurationBootLocalMachine = "//!/BootDeviceVolume/System/Config/MACHI";

struct ConfigurationKeyTag {
  [[maybe_unused]] static constexpr uint64_t seed = 18366205674763934000ull;
};

// Used for Key hashing
using ConfigurationKey = kernel::StaticIdentifier<ConfigurationKeyTag>;

// True once `root` is ready for use: either it already has an open backing
// file, or it is a volatile (file-less) hive, which is always considered
// "open" (its `roots` list simply lives in memory for as long as the root
// object itself does).
bool IsRootReady(const kernel::KConfigurationRoot &root) {
  return root.file != nullptr;
}

// Opens `configRoot`'s backing file (unless another key sharing the same
// root already has) and loads its on-disk index into memory via
// configuration_manager_io::LoadConfigurationIndex(). The file must already
// exist and contain a valid hive (correct magic/header and a readable
// INDEX BLOCK chain): tooling/tests that need a fresh, empty
// hive to register must build one first with
// configuration_manager_io::FormatEmptyConfigurationFile().
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

  configRoot->file = KE_VFS_OpenFile(configRoot->filePath,
                                     kernel::KFileObject::OpenMode::ReadWrite);
  if (!configRoot->file) {
    // Unable to open file
    early_print_fmt("ERROR: Unable to open configuration {}\r\n", configRoot->filePath);
    return false;
  }

  if (!LoadConfigurationIndex(configRoot->file, configRoot->roots, configRoot->numRoots)) {
    // Missing/invalid header, or the INDEX BLOCK chain couldn't be read:
    // the file must exist and already be a valid hive.
    early_print_fmt("ERROR: Unable to read configuration header {}\r\n", configRoot->filePath);
    KE_VFS_CloseFile(kstd::move(configRoot->file));
    configRoot->file = nullptr;
    return false;
  }

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

template<typename T>
bool GetFixedValue(const kernel::KConfigurationKeyPtr &key, kernel::ConfigurationValueType type, T &outValue) {
  if (!key || !key->configRoot) return false;

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!IsRootReady(*key->configRoot)) return false;

  T value{};
  if (!ReadKeyValue(*key->configRoot, key->keyHash, key->parentHash, type, kstd::span(reinterpret_cast<uint8_t *>(&value), sizeof(T)))) {
    return false;
  }
  outValue = value;
  return true;
}

template<typename T>
bool SetFixedValue(const kernel::KConfigurationKeyPtr &key, kernel::ConfigurationValueType type, const T &value) {
  if (!key || !key->configRoot) return false;

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!IsRootReady(*key->configRoot)) return false;

  return WriteKeyValue(*key->configRoot, key->keyHash, key->parentHash, type, kstd::span(reinterpret_cast<const uint8_t *>(&value), sizeof(T)));
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
  if (const auto hiveRoot = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigDirectory(), rootName)) {
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
    obj->keyHash = 0;   // The root itself has no path component to hash
    obj->parentHash = 0;// The root itself has no parent
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
    root = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigDirectory(), subKeyName.substr(0, firstSlash));
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
  const uint64_t expectedParentHash = parentKey ? parentKey->keyHash : 0;

  for (const auto &child: root->roots) {
    if (child.keyHash == realKeyHash && child.parentHash == expectedParentHash) {
      if (const auto obj = KE_CreateKPtrObject<kernel::KConfigurationKey>()) {
        // Same ordering requirement as KE_CM_OpenRoot: assign configRoot
        // first so the reference-count bump below can find it.
        obj->configRoot = root;
        obj->keyHash = child.keyHash;
        obj->parentHash = child.parentHash;
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
    root = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigDirectory(), subKeyName.substr(0, firstSlash));
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

    // Same parentHash scoping as KE_CM_OpenKey(): keyHash alone can collide
    // across different parents, so an "already exists" check must also
    // confirm the match is a direct child of this specific parentKey.
    const uint64_t expectedParentHash = parentKey ? parentKey->keyHash : 0;
    bool existsAlready = false;
    for (const auto &child: root->roots) {
      if (child.keyHash == realKeyHash && child.parentHash == expectedParentHash) {
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
        obj->parentHash = expectedParentHash;
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
            obj->parentHash = newEntry.parentHash;
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
    root = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigDirectory(), subKeyName.substr(0, firstSlash));
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

  // Same parentHash scoping as KE_CM_OpenKey()/KE_CM_CreateKey(): keyHash
  // alone can collide across different parents.
  const uint64_t expectedParentHash = parentKey ? parentKey->keyHash : 0;

  {
    kstd::unique_lock lock(root->mtx);

    for (auto it = root->roots.begin(); it != root->roots.end(); ++it) {
      if (it->keyHash == realKeyHash && it->parentHash == expectedParentHash) {
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
  if (!IsRootReady(*root)) return false;

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
    if (entry.keyHash == keyHandle->keyHash && entry.parentHash == keyHandle->parentHash) {
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
  if (!IsRootReady(*key->configRoot)) return false;

  for (const auto &entry: key->configRoot->roots) {
    if (entry.keyHash == key->keyHash && entry.parentHash == key->parentHash) {
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
  if (!IsRootReady(*key->configRoot)) return false;

  return WriteKeyValue(*key->configRoot, key->keyHash, key->parentHash, kernel::ConfigurationValueType::String,
                       kstd::span(reinterpret_cast<const uint8_t *>(outValue.data()), outValue.size()));
}
// endregion

UNDOS_KERNEL_API_DEF bool KE_CM_DeleteValue(const kernel::KConfigurationKeyPtr &keyHandle, kstd::string_view valueName) noexcept {
  if (!keyHandle || !keyHandle->configRoot) return false;

  // Same hash resolution as KE_CM_OpenKey()/KE_CM_CreateKey() when a
  // parentKey is given: valueName is hashed on its own, keyHandle is only
  // used to reach the right hive. The value entry is a direct child of
  // keyHandle, so its parentHash must equal keyHandle's own keyHash - see
  // KConfigurationKey::parentHash for why keyHash alone isn't enough.
  const ConfigurationKey realValueHash = ConfigurationKey::from_string(valueName);
  const auto &root = keyHandle->configRoot;

  kstd::unique_lock lock(root->mtx);
  if (!IsRootReady(*root)) return false;

  for (auto &entry: root->roots) {
    if (entry.keyHash == realValueHash && entry.parentHash == keyHandle->keyHash) {
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
  if (!IsRootReady(*root)) return false;

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
  if (!IsRootReady(*root)) return false;

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
  if (KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigDirectory(), hiveName)) {
    return nullptr;
  }

  auto hive = kernel::CreateKObject<kernel::KConfigurationRoot>(hiveName);
  if (!hive) {
    return nullptr;
  }
  hive->filePath = filePath;

  if (!KE_OB_InsertObject(KE_OB_GetConfigDirectory(), hive)) {
    return nullptr;
  }
  
  return hive;
}

namespace kernel::configuration_manager {
void init() {
  KE_CM_RegisterHive("LocalMachine", kConfigurationBootLocalMachine);
}

bool RebindHiveFile(const kstd::string_view &hiveName, const kstd::string_view &newFilePath) {
  const auto configRoot = KE_OB_FindDirectChildOfType<KConfigurationRoot>(KE_OB_GetConfigDirectory(), hiveName);
  if (!configRoot) {
    early_print_fmt("RebindHiveFile: hive '{}' is not registered\r\n", hiveName);
    return false;
  }

  kstd::unique_lock lock(configRoot->mtx);

  // Track whether we already held an open, indexed handle against the old
  // path. If we did, the in-memory `roots` index is valid for the same
  // on-disk bytes and must be preserved across the handle swap. If we did
  // not, leave the file closed after updating `filePath` so the next
  // EnsureRootIsOpened() performs a normal open+parse (or format) against
  // the new path - reopening here without parsing would leave `roots` empty
  // while marking the hive "ready", permanently skipping whatever is already
  // on disk at `newFilePath`.
  const bool hadOpenFile = configRoot->file != nullptr;

  if (configRoot->file) {
    KE_VFS_CloseFile(kstd::move(configRoot->file));
    configRoot->file = nullptr;
  }

  configRoot->filePath = newFilePath;

  if (!hadOpenFile) {
    // Path updated; lazy open+parse on next use.
    return true;
  }

  // Same underlying bytes as before, just reached through a different VFS
  // mount: reopen without re-parsing so `roots` stays intact.
  configRoot->file = KE_VFS_OpenFile(configRoot->filePath,
                                     KFileObject::OpenMode::ReadWrite |
                                         KFileObject::OpenMode::Create);
  if (!configRoot->file) {
    early_print_fmt("RebindHiveFile: failed to reopen hive '{}' at '{}'\r\n",
                    hiveName, newFilePath);
    return false;
  }

  return true;
}

void PromoteBootHive() {
  if (!RebindHiveFile("LocalMachine", kConfigurationLocalMachine)) {
    early_print_fmt("PromoteBootHive: failed to rebind LocalMachine onto the root mount\r\n");
  }
}
}// namespace kernel::configuration_manager
