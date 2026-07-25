
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
|  - Path Hash: uint64_t (8 Bytes, ObjectType value)                    |
|  - Value Type: uint8_t  (1 Byte, ConfigurationValueType)              |
|  - Data Offset: uint32_t (4 Bytes, relative to start of file)         |
|  - Data Length: uint16_t (2 Bytes, byte count of payload)             |
+-----------------------------------------------------------------------+
| DATA REGION                                                           |
|  - Raw Payloads (Little-Endian encoded primitive values or bytes)     |
+-----------------------------------------------------------------------+
*/

namespace {
constexpr kstd::array<uint8_t, 4> MAGIC = {'C', 'F', 'G', '1'};

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

bool EnsureRootIsOpened(kernel::KConfigurationKey &key) {
  if (!key.configRoot) {
    // Key was misconfigured: doesn't have a valid root
    return false;
  }

  kstd::unique_lock lock(key.configRoot->mtx);
  if (key.configRoot->file) {
    // Already opened and indexed by another key sharing the same root
    return true;
  }

  key.configRoot->file = KE_VFS_OpenFile(key.configRoot->filePath, kernel::KFileObject::OpenMode::ReadWrite);
  if (!key.configRoot->file) {
    // Unable to open file
    return false;
  }

  // Parse the header and put the offset in key.configRoot
  ConfigurationFileHeader expectedHeader{};

  {
    kstd::span expectedHeaderData(reinterpret_cast<uint8_t *>(&expectedHeader), sizeof(ConfigurationFileHeader));
    if (KE_VFS_ReadFile(key.configRoot->file, expectedHeaderData) != sizeof(ConfigurationFileHeader)) {
      // Wasn't able to read the header
      KE_VFS_CloseFile(kstd::move(key.configRoot->file));
      key.configRoot->file = nullptr;
      return false;
    }
  }

  if (expectedHeader.magic != MAGIC) {
    // Wrong magic
    KE_VFS_CloseFile(kstd::move(key.configRoot->file));
    key.configRoot->file = nullptr;
    return false;
  }

  key.configRoot->numRoots = expectedHeader.keyCount;

  // Read only the (lightweight) index table into memory; the actual value
  // payloads stay on disk and are read on demand when a value is queried.
  for (size_t i = 0; i < key.configRoot->numRoots; i++) {
    ConfigurationIndexEntry entry{};
    kstd::span entryData(reinterpret_cast<uint8_t *>(&entry), sizeof(ConfigurationIndexEntry));
    if (KE_VFS_ReadFile(key.configRoot->file, entryData) != sizeof(ConfigurationIndexEntry)) {
      KE_VFS_CloseFile(kstd::move(key.configRoot->file));
      key.configRoot->file = nullptr;
      key.configRoot->roots.clear();
      return false;
    }

    key.configRoot->roots.emplace_back(kernel::KConfigurationRoot::Root{
        entry.keyHash,
        static_cast<kernel::ConfigurationValueType>(entry.valueType),
        entry.offset,
        entry.length});
  }

  return true;
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

  auto &root = key->configRoot;
  if (root->configptr_references > 0) {
    root->configptr_references--;
  }

  // If no keys remain, close backing file and release in-memory index
  if (root->configptr_references == 0) {
    if (root->file) {
      KE_VFS_CloseFile(kstd::move(root->file));
      root->file = nullptr;
    }

    root->roots.clear();
  }

  return true;
}

UNDOS_KERNEL_API_DEF kernel::KConfigurationKeyPtr KE_CM_OpenRoot(const kstd::string_view &rootName) noexcept {
  if (const auto hiveRoot = KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigRoot(), rootName)) {
    auto obj = KE_CreateKPtrObject<kernel::KConfigurationKey>();
    obj->configRoot = hiveRoot;
    if (!EnsureRootIsOpened(*obj)) {
      return nullptr;
    }
    return obj;
  }
  return nullptr;
}

UNDOS_KERNEL_API_DEF kernel::KConfigurationKeyPtr KE_CM_OpenKey(const kernel::KConfigurationKeyPtr &parentKey,
                                                                const kstd::string_view &subKeyName,
                                                                kernel::KConfigurationKeyAccess desiredAccess) noexcept {
  (void) parentKey;
  (void) subKeyName;
  (void) desiredAccess;
  return nullptr;
}

UNDOS_KERNEL_API_DEF kernel::KConfigurationKeyPtr KE_CM_CreateKey(const kernel::KConfigurationKeyPtr &parentKey,
                                                                  const kstd::string_view &subKeyName,
                                                                  kernel::KConfigurationKeyAccess desiredAccess) noexcept {
  (void) parentKey;
  (void) subKeyName;
  (void) desiredAccess;
  return nullptr;
}

UNDOS_KERNEL_API_DEF bool KE_CM_DeleteKey(const kernel::KConfigurationKeyPtr &parentKey,
                                          const kstd::string_view &subKeyName) noexcept {
  (void) parentKey;
  (void) subKeyName;
  return false;
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
  (void) key;
  (void) outValue;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueUInt32(const kernel::KConfigurationKeyPtr &key, uint32_t &outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueUInt64(const kernel::KConfigurationKeyPtr &key, uint64_t &outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueSInt32(const kernel::KConfigurationKeyPtr &key, int32_t &outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueSInt64(const kernel::KConfigurationKeyPtr &key, int64_t &outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_GetKeyValueString(const kernel::KConfigurationKeyPtr &key, kstd::string_view &outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}
// endregion

// region Configuration Set Value
UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueBool(const kernel::KConfigurationKeyPtr &key, bool outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueUInt32(const kernel::KConfigurationKeyPtr &key, uint32_t outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueUInt64(const kernel::KConfigurationKeyPtr &key, uint64_t outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueSInt32(const kernel::KConfigurationKeyPtr &key, int32_t outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueSInt64(const kernel::KConfigurationKeyPtr &key, int64_t outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_CM_SetKeyValueString(const kernel::KConfigurationKeyPtr &key, const kstd::string_view &outValue) noexcept {
  (void) key;
  (void) outValue;
  return false;
}
// endregion

UNDOS_KERNEL_API_DEF bool KE_CM_DeleteValue(const kernel::KConfigurationKeyPtr &keyHandle, kstd::string_view valueName) noexcept {
  (void) keyHandle;
  (void) valueName;
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
