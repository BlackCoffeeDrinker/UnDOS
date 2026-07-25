
#pragma once

#include <atomic.hpp>
#include <chrono.hpp>
#include <kernel/fwd/KConfigurationKey.hpp>

namespace kernel {
enum class ConfigurationValueType {
  Invalid,
  Boolean,
  UInt32,
  UInt64,
  SInt32,
  SInt64,
  String,
};

enum class KConfigurationKeyAccess : uint16_t {
  READ = 1,
  WRITE = 2,
  RW = READ | WRITE,
};

struct KConfigurationKeyInfo {
  kstd::chrono::system_time lastModified;
  kstd::static_string<64> name;
  bool isFolder;
  ConfigurationValueType typeIfNotFolder;
};

struct KConfigurationKey {
  KObjectPtr<KConfigurationRoot> configRoot;
  KConfigurationKeyAccess access;

  // Own lifetime reference count for this handle object itself (distinct
  // from configRoot->configptr_references, which tracks how many open keys
  // reference the shared root). Managed by KConfigurationKeyPtrTraits; starts
  // at 0 since ownership is only granted once a KConfigurationKeyPtr adopts
  // the freshly-created object.
  kstd::atomic<uint32_t> handle_references{0};
};
}// namespace kernel

// Refcount methods are in include/kernel/fwd/KConfigurationKey.hpp

UNDOS_HAL_PUBLIC_V1API(
    kernel::KConfigurationKeyPtr,
    KE_CM_OpenRoot,
    const kstd::string_view &rootName);

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KConfigurationKeyPtr,
    KE_CM_OpenKey,
    const kernel::KConfigurationKeyPtr &parentKey,
    const kstd::string_view &subKeyName,
    kernel::KConfigurationKeyAccess desiredAccess = kernel::KConfigurationKeyAccess::READ);

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KConfigurationKeyPtr,
    KE_CM_CreateKey,
    const kernel::KConfigurationKeyPtr &parentKey,
    const kstd::string_view &subKeyName,
    kernel::KConfigurationKeyAccess desiredAccess = kernel::KConfigurationKeyAccess::RW);

UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_CM_DeleteKey,
    const kernel::KConfigurationKeyPtr &parentKey,
    const kstd::string_view &subKeyName);

UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_CM_FlushKey,
    const kernel::KConfigurationKeyPtr &keyHandle);

UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KM_CM_GetKeyInfo,
    const kernel::KConfigurationKeyPtr &keyHandle,
    kernel::KConfigurationKeyInfo &outInfo);

// region Configuration Get Value
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_GetKeyValueBool, const kernel::KConfigurationKeyPtr &key, bool &outValue);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_GetKeyValueUInt32, const kernel::KConfigurationKeyPtr &key, uint32_t &outValue);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_GetKeyValueUInt64, const kernel::KConfigurationKeyPtr &key, uint64_t &outValue);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_GetKeyValueSInt32, const kernel::KConfigurationKeyPtr &key, int32_t &outValue);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_GetKeyValueSInt64, const kernel::KConfigurationKeyPtr &key, int64_t &outValue);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_GetKeyValueString, const kernel::KConfigurationKeyPtr &key, kstd::string_view &outValue);
// endregion

// region Configuration Set Value
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_SetKeyValueBool, const kernel::KConfigurationKeyPtr &key, bool outValue);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_SetKeyValueUInt32, const kernel::KConfigurationKeyPtr &key, uint32_t outValue);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_SetKeyValueUInt64, const kernel::KConfigurationKeyPtr &key, uint64_t outValue);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_SetKeyValueSInt32, const kernel::KConfigurationKeyPtr &key, int32_t outValue);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_SetKeyValueSInt64, const kernel::KConfigurationKeyPtr &key, int64_t outValue);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_SetKeyValueString, const kernel::KConfigurationKeyPtr &key, const kstd::string_view &outValue);
// endregion

UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_CM_DeleteValue,
    const kernel::KConfigurationKeyPtr &keyHandle,
    kstd::string_view valueName);

UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_CM_EnumKey,
    const kernel::KConfigurationKeyPtr &keyHandle,
    uint32_t index,
    kstd::string_view &outName);

UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_CM_EnumValue,
    const kernel::KConfigurationKeyPtr &keyHandle,
    uint32_t index,
    kstd::string_view &outName,
    kernel::ConfigurationValueType &outType);
