
#pragma once

#include <atomic.hpp>
#include <expected.hpp>
#include <kernel/kerror.hpp>
#include <chrono.hpp>
#include <span.hpp>
#include <kernel/configuration_manager/configuration_key.hpp>
#include <kernel/configuration_manager/configuration_key_access.hpp>
#include <kernel/configuration_manager/configuration_key_info.hpp>
#include <kernel/configuration_manager/configuration_value_type.hpp>
#include <kernel/fwd/KConfigurationKey.hpp>

namespace kernel {
using KBoolResult = kstd::expected<bool, KernelError>;
using KConfigurationEnumKeyResult = kstd::expected<KConfigurationKeyEntry, KernelError>;
using KConfigurationEnumValueResult = kstd::expected<KConfigurationValueEntry, KernelError>;
using KConfigurationKeyResult = kstd::expected<kernel::KConfigurationKeyPtr, KernelError>;
using KConfigurationRootResult = kstd::expected<kernel::KObjectPtr<kernel::KConfigurationRoot>, KernelError>;
using KSInt32Result = kstd::expected<int32_t, KernelError>;
using KSInt64Result = kstd::expected<int64_t, KernelError>;
using KSizeResult = kstd::expected<size_t, KernelError>;
using KUInt32Result = kstd::expected<uint32_t, KernelError>;
using KUInt64Result = kstd::expected<uint64_t, KernelError>;
}// namespace kernel


// Refcount methods are in include/kernel/fwd/KConfigurationKey.hpp

/**
 * @brief Registers a new configuration hive (e.g. "LocalMachine") so it can
 * later be opened with KE_CM_OpenRoot()/KE_CM_OpenKey(). The backing file
 * doesn't need to exist yet: it is created and formatted lazily the first
 * time the hive is opened.
 * @param hiveName The unique name of the hive to register.
 * @param filePath The VFS path of the hive's backing file.
 * @return The newly registered hive, AlreadyExists if a hive with that name
 * is registered, or the error that made registration fail.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KConfigurationRootResult,
    KE_CM_RegisterHive,
    const kstd::string_view &hiveName,
    const kstd::string_view &filePath);


UNDOS_HAL_PUBLIC_V1API(
    kernel::KConfigurationKeyResult,
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
    kernel::KConfigurationKeyResult,
    KE_CM_OpenKey,
    const kernel::KConfigurationKeyPtr &parentKey,
    const kstd::string_view &subKeyName,
    kernel::KConfigurationKeyAccess desiredAccess = kernel::KConfigurationKeyAccess::Read);

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KConfigurationKeyResult,
    KE_CM_CreateKey,
    const kernel::KConfigurationKeyPtr &parentKey,
    const kstd::string_view &subKeyName,
    kernel::KConfigurationKeyAccess desiredAccess = kernel::KConfigurationKeyAccess::ReadWrite);

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_CM_DeleteKey,
    const kernel::KConfigurationKeyPtr &parentKey,
    const kstd::string_view &subKeyName);

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_CM_FlushKey,
    const kernel::KConfigurationKeyPtr &keyHandle);

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KM_CM_GetKeyInfo,
    const kernel::KConfigurationKeyPtr &keyHandle,
    kernel::KConfigurationKeyInfo &outInfo);

// region Configuration Get Value
UNDOS_KERNEL_PUBLIC_V1API(kernel::KBoolResult, KE_CM_GetKeyValueBool, const kernel::KConfigurationKeyPtr &key);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KUInt32Result, KE_CM_GetKeyValueUInt32, const kernel::KConfigurationKeyPtr &key);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KUInt64Result, KE_CM_GetKeyValueUInt64, const kernel::KConfigurationKeyPtr &key);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KSInt32Result, KE_CM_GetKeyValueSInt32, const kernel::KConfigurationKeyPtr &key);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KSInt64Result, KE_CM_GetKeyValueSInt64, const kernel::KConfigurationKeyPtr &key);
// Copies the string (not NUL-terminated) into outValue and returns its length; BufferTooSmall if it does not fit.
UNDOS_KERNEL_PUBLIC_V1API(kernel::KSizeResult, KE_CM_GetKeyValueString, const kernel::KConfigurationKeyPtr &key, kstd::span<char> outValue);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KSizeResult, KE_CM_GetKeyValueStringSize, const kernel::KConfigurationKeyPtr &key);
// endregion

// region Configuration Set Value
UNDOS_KERNEL_PUBLIC_V1API(kernel::KernelError, KE_CM_SetKeyValueBool, const kernel::KConfigurationKeyPtr &key, bool outValue);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KernelError, KE_CM_SetKeyValueUInt32, const kernel::KConfigurationKeyPtr &key, uint32_t outValue);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KernelError, KE_CM_SetKeyValueUInt64, const kernel::KConfigurationKeyPtr &key, uint64_t outValue);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KernelError, KE_CM_SetKeyValueSInt32, const kernel::KConfigurationKeyPtr &key, int32_t outValue);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KernelError, KE_CM_SetKeyValueSInt64, const kernel::KConfigurationKeyPtr &key, int64_t outValue);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KernelError, KE_CM_SetKeyValueString, const kernel::KConfigurationKeyPtr &key, const kstd::string_view &outValue);
// endregion

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_CM_DeleteValue,
    const kernel::KConfigurationKeyPtr &keyHandle,
    kstd::string_view valueName);

// Returns the index-th child key. Success with available=false means enumeration is exhausted.
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KConfigurationEnumKeyResult,
    KE_CM_EnumKey,
    const kernel::KConfigurationKeyPtr &keyHandle,
    uint32_t index);

// Same as KE_CM_EnumKey for the values of a key.
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KConfigurationEnumValueResult,
    KE_CM_EnumValue,
    const kernel::KConfigurationKeyPtr &keyHandle,
    uint32_t index);
