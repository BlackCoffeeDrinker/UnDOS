
#pragma once

#include <atomic.hpp>
#include <chrono.hpp>
#include <kernel/configuration_manager/configuration_key.hpp>
#include <kernel/configuration_manager/configuration_key_access.hpp>
#include <kernel/configuration_manager/configuration_key_info.hpp>
#include <kernel/configuration_manager/configuration_value_type.hpp>
#include <kernel/fwd/KConfigurationKey.hpp>

// Refcount methods are in include/kernel/fwd/KConfigurationKey.hpp

/**
 * @brief Registers a new configuration hive (e.g. "LocalMachine") so it can
 * later be opened with KE_CM_OpenRoot()/KE_CM_OpenKey(). The backing file
 * doesn't need to exist yet: it is created and formatted lazily the first
 * time the hive is opened.
 * @param hiveName The unique name of the hive to register.
 * @param filePath The VFS path of the hive's backing file.
 * @return The newly registered hive, or nullptr if a hive with that name
 * already exists or registration otherwise failed.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KConfigurationRoot>,
    KE_CM_RegisterHive,
    const kstd::string_view &hiveName,
    const kstd::string_view &filePath);

/**
 * @brief Registers a new volatile (file-less, in-memory-only) configuration
 * hive. Unlike KE_CM_RegisterHive(), this never touches the VFS: the hive's
 * keys live purely in memory for as long as the hive object itself does, so
 * it can be used before any disk/filesystem driver exists (e.g. during the
 * boot-driver phase).
 * @param hiveName The unique name of the hive to register.
 * @return The newly registered hive, or nullptr if a hive with that name
 * already exists or registration otherwise failed.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KConfigurationRoot>,
    KE_CM_RegisterVolatileHive,
    const kstd::string_view &hiveName);

UNDOS_HAL_PUBLIC_V1API(
    kernel::KConfigurationKeyPtr,
    KE_CM_OpenRoot,
    const kstd::string_view &rootName);

/**
 * 
 * @param parentKey Optional handle to the root object directory for the path name specified by the subKeyName member. If parentKey is NULL, subKeyName must point to a fully qualified object name that includes the full path to the target object. If parentKey is non-NULL, subKeyName specifies an object name relative to the parentKey directory.
 * @param subKeyName This must either be a fully qualified object name, or a relative path name to the directory specified by the parentKey member.
 * @param desiredAccess 
 * @return 
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KConfigurationKeyPtr,
    KE_CM_OpenKey,
    const kernel::KConfigurationKeyPtr &parentKey,
    const kstd::string_view &subKeyName,
    kernel::KConfigurationKeyAccess desiredAccess = kernel::KConfigurationKeyAccess::Read);

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KConfigurationKeyPtr,
    KE_CM_CreateKey,
    const kernel::KConfigurationKeyPtr &parentKey,
    const kstd::string_view &subKeyName,
    kernel::KConfigurationKeyAccess desiredAccess = kernel::KConfigurationKeyAccess::ReadWrite);

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
