
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

struct ConfigurationKeyTag {
  [[maybe_unused]] static constexpr uint64_t seed = 18366205674763934000ull;
};

// Used for Key hashing
using ConfigurationKey = kernel::StaticIdentifier<ConfigurationKeyTag>;

bool IsRootReady(const kernel::KConfigurationRoot &root) { return root.file != nullptr && root.indexLoaded; }

// Caller holds configRoot->mtx.
kstd::error_code EnsureRootIsOpenedLocked(const kernel::KObjectPtr<kernel::KConfigurationRoot> &configRoot) {
  if (!configRoot) {
    // Key was misconfigured, or the hive is not registered
    KE_Print("ERROR: Configuration root not found");
    return kernel::KernelError::NotFound;
  }

  if (!configRoot->file) {
    KE_Print("HIVE Root {} is not opened...", configRoot->name);
    auto opened = KE_VFS_OpenFile(configRoot->filePath, kernel::KIOObject::OpenMode::ReadWrite);
    if (!opened) {
      KE_Print("ERROR: Unable to open configuration {}", configRoot->filePath);
      return opened.error();
    }
    configRoot->file = kstd::move(*opened);
  }

  if (!configRoot->file->mountPoint || !configRoot->file->mountPoint->volume) {
    KE_VFS_CloseFile(kstd::move(configRoot->file));
    configRoot->file = nullptr;
    return kernel::KernelError::InvalidState;
  }
  const auto currentVolume = configRoot->file->mountPoint->volume;
  if (configRoot->indexLoaded && configRoot->loadedVolume.get() == currentVolume.get()) return {};

  kstd::list<kernel::KConfigurationRoot::Root> loadedRoots;
  size_t loadedCount = 0;
  if (const auto status = LoadConfigurationIndex(configRoot->file, loadedRoots, loadedCount); status) {
    KE_Print("ERROR: Unable to read configuration header {}", configRoot->filePath);
    KE_VFS_CloseFile(kstd::move(configRoot->file));
    configRoot->file = nullptr;
    configRoot->roots.clear();
    configRoot->numRoots = 0;
    configRoot->loadedVolume = nullptr;
    configRoot->indexLoaded = false;
    return status;
  }

  configRoot->roots = kstd::move(loadedRoots);
  configRoot->numRoots = loadedCount;
  configRoot->loadedVolume = currentVolume;
  configRoot->indexLoaded = true;
  return {};
}

 kstd::error_code IncrementConfigRootReferenceLocked(kernel::KConfigurationRoot &root) {
  if (root.configptr_references == UINT32_MAX) return kernel::KernelError::ResourceLimitExceeded;
  ++root.configptr_references;
  return {};
}

void ReleaseConfigRootReferenceLocked(kernel::KConfigurationRoot &root) {
  if (root.configptr_references > 1) --root.configptr_references;
}

kstd::error_code OpenAndReferenceConfigRoot(const kernel::KObjectPtr<kernel::KConfigurationRoot> &root) {
  if (!root) return kernel::KernelError::NotFound;
  kstd::unique_lock lock(root->mtx);
  if (const auto status = EnsureRootIsOpenedLocked(root); status) return status;
  return IncrementConfigRootReferenceLocked(*root);
}

void ReleaseConfigRootReference(const kernel::KObjectPtr<kernel::KConfigurationRoot> &root) {
  if (!root) return;
  kstd::unique_lock lock(root->mtx);
  ReleaseConfigRootReferenceLocked(*root);
}

template<typename T>
kstd::expected<T, kstd::error_code> GetFixedValue(const kernel::KConfigurationKeyPtr &key, kernel::ConfigurationValueType type) {
  if (!key || !key->configRoot) return kstd::unexpected(kernel::KernelError::InvalidHandle);

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!IsRootReady(*key->configRoot)) return kstd::unexpected(kernel::KernelError::NotInitialized);

  T value{};
  if (const auto status = ReadKeyValue(*key->configRoot, key->keyHash, key->parentHash, type, kstd::span(reinterpret_cast<uint8_t *>(&value), sizeof(T)));
      status) {
    return kstd::unexpected(status);
  }
  return value;
}

template<typename T>
kstd::error_code SetFixedValue(const kernel::KConfigurationKeyPtr &key, kernel::ConfigurationValueType type, const T &value) {
  if (!key || !key->configRoot) return kernel::KernelError::InvalidHandle;

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!IsRootReady(*key->configRoot)) return kernel::KernelError::NotInitialized;

  return WriteKeyValue(*key->configRoot, key->keyHash, key->parentHash, type, kstd::span(reinterpret_cast<const uint8_t *>(&value), sizeof(T)));
}
}// namespace

UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_IncrementConfigRootReferenceCount(const kernel::KConfigurationKey *key) noexcept {
  if (!key || !key->configRoot) return kernel::KernelError::InvalidHandle;
  kstd::unique_lock lock(key->configRoot->mtx);
  return IncrementConfigRootReferenceLocked(*key->configRoot);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_DecrementConfigRootReferenceCount(const kernel::KConfigurationKey *key) noexcept {
  if (!key || !key->configRoot) return kernel::KernelError::InvalidHandle;

  ReleaseConfigRootReference(key->configRoot);
  return {};
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KConfigurationKeyPtr, kstd::error_code> KE_CM_OpenRoot(const kstd::string_view &rootName) noexcept {
  const auto hiveRoot = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigDirectory(), rootName);
  if (!hiveRoot) {
    KE_Print("Hive root {} not found", rootName);
    return kstd::unexpected(kernel::KernelError::NotFound);
  }

  if (const auto status = OpenAndReferenceConfigRoot(hiveRoot); status) {
    KE_Print("Hive {} is not opened", rootName);
    return kstd::unexpected(status);
  }

  auto created = KE_CreateKPtrObject<kernel::KConfigurationKey>();
  if (!created) {
    KE_Print("Failed to create KConfigurationKey for hive {}", rootName);
    ReleaseConfigRootReference(hiveRoot);
    return kstd::unexpected(created.error());
  }

  auto obj = kstd::move(*created);

  // configRoot must be assigned before bumping the reference count: the
  // count lives on the shared root, not on this handle, and
  // KE_CM_IncrementConfigRootReferenceCount() looks it up through it.
  obj->configRoot = hiveRoot;
  obj->keyHash = 0;   // The root itself has no path component to hash
  obj->parentHash = 0;// The root itself has no parent
  obj->access = kernel::KConfigurationKeyAccess::Key;
  if (const auto status = KE_CM_IncrementConfigRootReferenceCount(obj.get()); status) {
    obj->configRoot = nullptr;
    ReleaseConfigRootReference(hiveRoot);
    return kstd::unexpected(status);
  }
  ReleaseConfigRootReference(hiveRoot);
  return obj;
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KConfigurationKeyPtr, kstd::error_code> KE_CM_OpenKey(const kernel::KConfigurationKeyPtr &parentKey,
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
      return kstd::unexpected(kernel::KernelError::InvalidArgument);
    }
    realKeyHash = ConfigurationKey::from_string(subKeyName.substr(firstSlash + 1));
    root = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigDirectory(), subKeyName.substr(0, firstSlash));
  }

  if (const auto status = OpenAndReferenceConfigRoot(root); status) {
    // Cannot open root
    if (root) KE_Print("Hive root {} is not opened for {}", root->name, subKeyName);
    return kstd::unexpected(status);
  }

  kstd::expected<kernel::KConfigurationKeyPtr, kstd::error_code> found = kstd::unexpected(kernel::KernelError::NotFound);
  const uint64_t expectedParentHash = parentKey ? parentKey->keyHash : 0;

  {
    kstd::unique_lock lock(root->mtx);
    for (const auto &child: root->roots) {
      if (child.keyHash == realKeyHash && child.parentHash == expectedParentHash) {
        if (auto created = KE_CreateKPtrObject<kernel::KConfigurationKey>(); created) {
          auto obj = kstd::move(*created);
          obj->configRoot = root;
          obj->keyHash = child.keyHash;
          obj->parentHash = child.parentHash;
          obj->access = desiredAccess;
          if (const auto status = IncrementConfigRootReferenceLocked(*root); status) {
            obj->configRoot = nullptr;
            found = kstd::unexpected(status);
          } else {
            found = kstd::move(obj);
          }
        } else {
          found = kstd::unexpected(created.error());
        }
        break;
      }
    }
  }

  ReleaseConfigRootReference(root);
  return found;
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KConfigurationKeyPtr, kstd::error_code> KE_CM_CreateKey(const kernel::KConfigurationKeyPtr &parentKey,
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
      return kstd::unexpected(kernel::KernelError::InvalidArgument);
    }
    resolvedName = subKeyName.substr(firstSlash + 1);
    realKeyHash = ConfigurationKey::from_string(resolvedName);
    root = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigDirectory(), subKeyName.substr(0, firstSlash));
  }

  if (const auto status = OpenAndReferenceConfigRoot(root); status) {
    // Cannot open root
    return kstd::unexpected(status);
  }

  // Every path below either overwrites this or leaves the explicit failure.
  kstd::expected<kernel::KConfigurationKeyPtr, kstd::error_code> result = kstd::unexpected(kernel::KernelError::Unknown);

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
      result = kstd::unexpected(kernel::KernelError::AlreadyExists);
    } else if (existsAlready) {
      // Key already exists and the caller didn't require exclusivity:
      // behave like KE_CM_OpenKey() and just hand back a handle to it.
      if (auto created = KE_CreateKPtrObject<kernel::KConfigurationKey>(); created) {
        auto obj = kstd::move(*created);
        obj->configRoot = root;
        obj->keyHash = realKeyHash;
        obj->parentHash = expectedParentHash;
        obj->access = desiredAccess;
        if (const auto status = IncrementConfigRootReferenceLocked(*root); status) {
          obj->configRoot = nullptr;
          result = kstd::unexpected(status);
        } else {
          result = kstd::move(obj);
        }
      } else {
        result = kstd::unexpected(created.error());
      }
    } else {
      uint32_t indexBlockOffset = 0;
      uint32_t indexEntryOffset = 0;
      if (const auto slotStatus = FindOrAllocateFreeSlot(*root, indexBlockOffset, indexEntryOffset); !slotStatus) {
        // Persist a name entry alongside the index entry so KE_CM_EnumKey()/
        // KE_CM_EnumValue() can later report this key's short name. A
        // paired NAME BLOCK has fewer slots than its INDEX BLOCK (56 vs
        // 170), so this can fail even though the index slot was found: the
        // key is still created in that case, it is simply left unnamed.
        uint16_t nameEntryIndex = NO_NAME_ENTRY;
        if (FindOrAllocateNameSlot(root->file, indexBlockOffset, realKeyHash, resolvedName, nameEntryIndex)) {
          nameEntryIndex = NO_NAME_ENTRY;
        }

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

        if (const auto persistStatus = PersistIndexEntry(root->file, newEntry); !persistStatus) {
          root->roots.emplace_back(newEntry);
          root->numRoots++;

          if (auto created = KE_CreateKPtrObject<kernel::KConfigurationKey>(); created) {
            auto obj = kstd::move(*created);
            obj->configRoot = root;
            obj->keyHash = newEntry.keyHash;
            obj->parentHash = newEntry.parentHash;
            obj->access = desiredAccess;
            if (const auto status = IncrementConfigRootReferenceLocked(*root); status) {
              obj->configRoot = nullptr;
              result = kstd::unexpected(status);
            } else {
              result = kstd::move(obj);
            }
          } else {
            result = kstd::unexpected(created.error());
          }
        } else {
          result = kstd::unexpected(persistStatus);
        }
      } else {
        result = kstd::unexpected(slotStatus);
      }
    }
  }

  ReleaseConfigRootReference(root);
  return result;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_DeleteKey(const kernel::KConfigurationKeyPtr &parentKey,
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
      return kernel::KernelError::InvalidArgument;
    }
    realKeyHash = ConfigurationKey::from_string(subKeyName.substr(firstSlash + 1));
    root = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigDirectory(), subKeyName.substr(0, firstSlash));
  }

  if (const auto status = OpenAndReferenceConfigRoot(root); status) {
    // Cannot open root
    return status;
  }

  kstd::error_code deleteStatus = kernel::KernelError::NotFound;

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
        deleteStatus = PersistIndexEntry(root->file, cleared);
        if (!deleteStatus) {
          // Also free up the paired name slot (if any) so
          // FindOrAllocateNameSlot() can reuse it for a future key. A
          // failure here only leaks the name slot, the key is gone.
          if (it->nameEntryIndex != NO_NAME_ENTRY) {
            (void) PersistNameEntry(root->file, it->indexBlockOffset, it->nameEntryIndex, 0, kstd::string_view());
          }

          root->roots.erase(it);
          if (root->numRoots > 0) {
            root->numRoots--;
          }
        }
        break;
      }
    }
  }

  ReleaseConfigRootReference(root);
  return deleteStatus;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_FlushKey(const kernel::KConfigurationKeyPtr &keyHandle) noexcept {
  (void) keyHandle;
  return kernel::KernelError::NotImplemented;
}

UNDOS_KERNEL_API_DEF kstd::error_code KM_CM_GetKeyInfo(const kernel::KConfigurationKeyPtr &keyHandle,
                                                       kernel::KConfigurationKeyInfo &outInfo) noexcept {
  if (!keyHandle || !keyHandle->configRoot) return kernel::KernelError::InvalidHandle;

  const auto &root = keyHandle->configRoot;
  kstd::unique_lock lock(root->mtx);
  if (!IsRootReady(*root)) return kernel::KernelError::NotInitialized;

  if (keyHandle->keyHash == 0) {
    // The hive root itself: it has no matching index entry (it isn't a
    // regular key), but it always exists and is always folder-like.
    outInfo.lastModified = kstd::chrono::system_time{};
    outInfo.name.clear();
    outInfo.isFolder = true;
    outInfo.typeIfNotFolder = kernel::ConfigurationValueType::Invalid;
    return {};
  }

  for (const auto &entry: root->roots) {
    if (entry.keyHash == keyHandle->keyHash && entry.parentHash == keyHandle->parentHash) {
      // No on-disk modification-time tracking exists yet: default-initialize
      // rather than fabricate a value.
      outInfo.lastModified = kstd::chrono::system_time{};
      outInfo.name = entry.name;
      outInfo.isFolder = entry.valueType == kernel::ConfigurationValueType::Invalid;
      outInfo.typeIfNotFolder = entry.valueType;
      return {};
    }
  }
  return kernel::KernelError::NotFound;
}

// region Configuration Get Value
UNDOS_KERNEL_API_DEF kstd::expected<bool, kstd::error_code> KE_CM_GetKeyValueBool(const kernel::KConfigurationKeyPtr &key) noexcept {
  const auto raw = GetFixedValue<uint8_t>(key, kernel::ConfigurationValueType::Boolean);
  if (!raw) return kstd::unexpected(raw.error());
  return *raw != 0;
}

UNDOS_KERNEL_API_DEF kstd::expected<uint32_t, kstd::error_code> KE_CM_GetKeyValueUInt32(const kernel::KConfigurationKeyPtr &key) noexcept {
  return GetFixedValue<uint32_t>(key, kernel::ConfigurationValueType::UInt32);
}

UNDOS_KERNEL_API_DEF kstd::expected<uint64_t, kstd::error_code> KE_CM_GetKeyValueUInt64(const kernel::KConfigurationKeyPtr &key) noexcept {
  return GetFixedValue<uint64_t>(key, kernel::ConfigurationValueType::UInt64);
}

UNDOS_KERNEL_API_DEF kstd::expected<int32_t, kstd::error_code> KE_CM_GetKeyValueSInt32(const kernel::KConfigurationKeyPtr &key) noexcept {
  return GetFixedValue<int32_t>(key, kernel::ConfigurationValueType::SInt32);
}

UNDOS_KERNEL_API_DEF kstd::expected<int64_t, kstd::error_code> KE_CM_GetKeyValueSInt64(const kernel::KConfigurationKeyPtr &key) noexcept {
  return GetFixedValue<int64_t>(key, kernel::ConfigurationValueType::SInt64);
}

UNDOS_KERNEL_API_DEF kstd::expected<size_t, kstd::error_code> KE_CM_GetKeyValueString(const kernel::KConfigurationKeyPtr &key, kstd::span<char> outValue) noexcept {
  if (!key || !key->configRoot) [[unlikely]]
    return kstd::unexpected(kernel::KernelError::InvalidHandle);

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!IsRootReady(*key->configRoot)) [[unlikely]]
    return kstd::unexpected(kernel::KernelError::NotInitialized);

  for (const auto &entry: key->configRoot->roots) {
    if (entry.keyHash == key->keyHash && entry.parentHash == key->parentHash) {
      if (entry.valueType != kernel::ConfigurationValueType::String) {
        return kstd::unexpected(kernel::KernelError::InvalidFormat);
      }
      if (entry.length > outValue.size()) {
        return kstd::unexpected(kernel::KernelError::BufferTooSmall);
      }
      if (const auto status = ReadRawAt(key->configRoot->file,
                                        entry.offset,
                                        kstd::span(reinterpret_cast<uint8_t *>(outValue.data()), entry.length));
          status) {
        return kstd::unexpected(status);
      }

      return static_cast<size_t>(entry.length);
    }
  }
  return kstd::unexpected(kernel::KernelError::NotFound);
}

UNDOS_KERNEL_API_DEF kstd::expected<size_t, kstd::error_code> KE_CM_GetKeyValueStringSize(const kernel::KConfigurationKeyPtr &key) noexcept {
  if (!key || !key->configRoot) [[unlikely]]
    return kstd::unexpected(kernel::KernelError::InvalidHandle);

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!IsRootReady(*key->configRoot)) [[unlikely]]
    return kstd::unexpected(kernel::KernelError::NotInitialized);

  for (const auto &entry: key->configRoot->roots) {
    if (entry.keyHash == key->keyHash && entry.parentHash == key->parentHash) {
      if (entry.valueType != kernel::ConfigurationValueType::String) {
        return kstd::unexpected(kernel::KernelError::InvalidFormat);
      }

      return static_cast<size_t>(entry.length);
    }
  }

  return kstd::unexpected(kernel::KernelError::NotFound);
}

// endregion

// region Configuration Set Value
UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_SetKeyValueBool(const kernel::KConfigurationKeyPtr &key, bool outValue) noexcept {
  const uint8_t raw = outValue ? 1 : 0;
  return SetFixedValue(key, kernel::ConfigurationValueType::Boolean, raw);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_SetKeyValueUInt32(const kernel::KConfigurationKeyPtr &key, uint32_t outValue) noexcept {
  return SetFixedValue(key, kernel::ConfigurationValueType::UInt32, outValue);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_SetKeyValueUInt64(const kernel::KConfigurationKeyPtr &key, uint64_t outValue) noexcept {
  return SetFixedValue(key, kernel::ConfigurationValueType::UInt64, outValue);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_SetKeyValueSInt32(const kernel::KConfigurationKeyPtr &key, int32_t outValue) noexcept {
  return SetFixedValue(key, kernel::ConfigurationValueType::SInt32, outValue);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_SetKeyValueSInt64(const kernel::KConfigurationKeyPtr &key, int64_t outValue) noexcept {
  return SetFixedValue(key, kernel::ConfigurationValueType::SInt64, outValue);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_SetKeyValueString(const kernel::KConfigurationKeyPtr &key, const kstd::string_view &outValue) noexcept {
  if (!key || !key->configRoot) return kernel::KernelError::InvalidHandle;

  kstd::unique_lock lock(key->configRoot->mtx);
  if (!IsRootReady(*key->configRoot)) return kernel::KernelError::NotInitialized;

  return WriteKeyValue(*key->configRoot, key->keyHash, key->parentHash, kernel::ConfigurationValueType::String,
                       kstd::span(reinterpret_cast<const uint8_t *>(outValue.data()), outValue.size()));
}
// endregion

UNDOS_KERNEL_API_DEF kstd::error_code KE_CM_DeleteValue(const kernel::KConfigurationKeyPtr &keyHandle, kstd::string_view valueName) noexcept {
  if (!keyHandle || !keyHandle->configRoot) return kernel::KernelError::InvalidHandle;

  // Same hash resolution as KE_CM_OpenKey()/KE_CM_CreateKey() when a
  // parentKey is given: valueName is hashed on its own, keyHandle is only
  // used to reach the right hive. The value entry is a direct child of
  // keyHandle, so its parentHash must equal keyHandle's own keyHash - see
  // KConfigurationKey::parentHash for why keyHash alone isn't enough.
  const ConfigurationKey realValueHash = ConfigurationKey::from_string(valueName);
  const auto &root = keyHandle->configRoot;

  kstd::unique_lock lock(root->mtx);
  if (!IsRootReady(*root)) return kernel::KernelError::NotInitialized;

  for (auto &entry: root->roots) {
    if (entry.keyHash == realValueHash && entry.parentHash == keyHandle->keyHash) {
      if (entry.valueType == kernel::ConfigurationValueType::Invalid) {
        // No value currently set on this entry: nothing to delete.
        return kernel::KernelError::NotFound;
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
  return kernel::KernelError::NotFound;
}

UNDOS_KERNEL_API_DEF kernel::KConfigurationEnumKeyResult KE_CM_EnumKey(const kernel::KConfigurationKeyPtr &keyHandle, uint32_t index) noexcept {
  if (!keyHandle || !keyHandle->configRoot) return kstd::unexpected(kernel::KernelError::InvalidHandle);

  const auto &root = keyHandle->configRoot;
  kstd::unique_lock lock(root->mtx);
  if (!IsRootReady(*root)) return kstd::unexpected(kernel::KernelError::NotInitialized);

  // Enumerate only the folder-like (Invalid-typed) entries that are direct
  // children of keyHandle, in the same stable order they appear in
  // root->roots (the order they were loaded from / appended to the index).
  uint32_t matched = 0;
  for (const auto &entry: root->roots) {
    if (entry.parentHash != keyHandle->keyHash) continue;
    if (entry.valueType != kernel::ConfigurationValueType::Invalid) continue;

    if (matched == index) {
      keyHandle->enumNameScratch = entry.name;
      return kernel::KConfigurationKeyEntry{true, kstd::string_view(keyHandle->enumNameScratch)};
    }
    matched++;
  }
  return kernel::KConfigurationKeyEntry{};// end of enumeration
}

UNDOS_KERNEL_API_DEF kernel::KConfigurationEnumValueResult KE_CM_EnumValue(const kernel::KConfigurationKeyPtr &keyHandle, uint32_t index) noexcept {
  if (!keyHandle || !keyHandle->configRoot) return kstd::unexpected(kernel::KernelError::InvalidHandle);

  const auto &root = keyHandle->configRoot;
  kstd::unique_lock lock(root->mtx);
  if (!IsRootReady(*root)) return kstd::unexpected(kernel::KernelError::NotInitialized);

  // Same as KE_CM_EnumKey(), but for the value-holding (non-Invalid-typed)
  // direct children of keyHandle instead of the folder-like ones.
  uint32_t matched = 0;
  for (const auto &entry: root->roots) {
    if (entry.parentHash != keyHandle->keyHash) continue;
    if (entry.valueType == kernel::ConfigurationValueType::Invalid) continue;

    if (matched == index) {
      keyHandle->enumNameScratch = entry.name;
      return kernel::KConfigurationValueEntry{true, kstd::string_view(keyHandle->enumNameScratch), entry.valueType};
    }
    matched++;
  }
  return kernel::KConfigurationValueEntry{};// end of enumeration
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KConfigurationRoot>, kstd::error_code> KE_CM_RegisterHive(const kstd::string_view &hiveName, const kstd::string_view &filePath) noexcept {
  if (KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigDirectory(), hiveName)) {
    return kstd::unexpected(kernel::KernelError::AlreadyExists);
  }

  auto created = kernel::CreateKObject<kernel::KConfigurationRoot>(hiveName);
  if (!created) {
    return kstd::unexpected(created.error());
  }
  auto hive = kstd::move(*created);
  hive->filePath = filePath;
  hive->configptr_references = 1;

  if (const auto status = KE_OB_InsertObject(KE_OB_GetConfigDirectory(), hive); status) {
    return kstd::unexpected(status);
  }

  return hive;
}

namespace kernel::configuration_manager {
void init() {
  if (const auto hive = KE_CM_RegisterHive("LocalMachine", kConfigurationLocalMachine); !hive) {
    KE_Print("configuration_manager::init: failed to register LocalMachine hive");
  }
}

kstd::error_code PrepareRootVolumeSwitch() {
  const auto configRoot = KE_OB_FindDirectChildOfType<KConfigurationRoot>(KE_OB_GetConfigDirectory(), "LocalMachine");
  if (!configRoot) return KernelError::NotFound;

  kstd::unique_lock lock(configRoot->mtx);
  if (configRoot->configptr_references > 1) return KernelError::ResourceConflict;
  if (!configRoot->file) return {};
  if (const auto status = KE_VFS_CloseFile(configRoot->file); status) return status;
  configRoot->file = nullptr;
  return {};
}

kstd::error_code PromoteBootHive(const KDevicePtr<KDevice> &selectedVolume) {
  if (!selectedVolume) return KernelError::InvalidArgument;
  const auto configRoot = KE_OB_FindDirectChildOfType<KConfigurationRoot>(KE_OB_GetConfigDirectory(), "LocalMachine");
  if (!configRoot) return KernelError::NotFound;

  kstd::unique_lock lock(configRoot->mtx);
  if (configRoot->configptr_references > 1) return KernelError::ResourceConflict;

  auto opened = KE_VFS_OpenFile(configRoot->filePath, KIOObject::OpenMode::ReadWrite);
  if (!opened) {
    if (!configRoot->indexLoaded || configRoot->loadedVolume.get() != selectedVolume.get()) {
      configRoot->roots.clear();
      configRoot->numRoots = 0;
      configRoot->loadedVolume = nullptr;
      configRoot->indexLoaded = false;
    }
    return opened.error();
  }
  if (!(*opened)->mountPoint || (*opened)->mountPoint->volume.get() != selectedVolume.get()) {
    KE_VFS_CloseFile(*opened);
    return KernelError::InvalidState;
  }

  if (configRoot->indexLoaded && configRoot->loadedVolume.get() == selectedVolume.get()) {
    configRoot->file = kstd::move(*opened);
    return {};
  }

  kstd::list<KConfigurationRoot::Root> loadedRoots;
  size_t loadedCount = 0;
  if (const auto status = LoadConfigurationIndex(*opened, loadedRoots, loadedCount); status) {
    KE_VFS_CloseFile(*opened);
    configRoot->file = nullptr;
    configRoot->roots.clear();
    configRoot->numRoots = 0;
    configRoot->loadedVolume = nullptr;
    configRoot->indexLoaded = false;
    return status;
  }

  configRoot->file = kstd::move(*opened);
  configRoot->roots = kstd::move(loadedRoots);
  configRoot->numRoots = loadedCount;
  configRoot->loadedVolume = selectedVolume;
  configRoot->indexLoaded = true;
  return {};
}
}// namespace kernel::configuration_manager
