
#pragma once
#include <kernel/__core.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kerror.hpp>
#include <expected.hpp>

#include <static_string.hpp>
#include <string_view.hpp>

namespace kernel {
struct KObject;
struct KDirectoryObject;
using MakeUniqueNameResult = kstd::expected<uint32_t, kstd::error_code>;

constexpr kstd::static_string kOBRootPath = "/";
constexpr kstd::static_string kOBDevicesPath = "Device";
constexpr kstd::static_string kOBDriversPath = "Driver";
constexpr kstd::static_string kOBMemoryPath = "Memory";
constexpr kstd::static_string kOBThreadPath = "Threads";
constexpr kstd::static_string kOBProcessesPath = "Processes";
constexpr kstd::static_string kOBFileSystemPath = "FileSystem";
constexpr kstd::static_string kOBVFSPath = "VFS";
constexpr kstd::static_string kOBFilePath = "File";
constexpr kstd::static_string kOBSubsystemsPath = "Subsystems";
constexpr kstd::static_string kOBModulesPath = "Modules";
constexpr kstd::static_string kOBHivePath = "Hive";
constexpr kstd::static_string kOBSystemPath = "System";
constexpr kstd::static_string kOBGfxPath = "Gfx";

}// namespace kernel

/**
 * @ingroup OB
 * @brief Method KE_OB_InsertObject
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_OB_InsertObject,
    const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kernel::KObjectPtr<kernel::KObject> &child);


/**
 * @ingroup OB
 * @brief Method KE_OB_InsertObject
 * 
 * Returns the number to append to `baseName` for a unique name.
 * Example, for baseName `Display`, if parent has Display0, Display1: returns 2
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::MakeUniqueNameResult,
    KE_OB_MakeUniqueName,
    const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kstd::string_view &baseName);

/**
 * @ingroup OB
 * @brief Method KE_OB_RemoveObject
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_OB_RemoveObject,
    const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kernel::KObjectPtr<kernel::KObject> &child);

/**
 * @ingroup OB
 * @brief Method KE_OB_LookupObject
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KObject>,
    KE_OB_LookupObject,
    const kstd::string_view &path);

/**
 * @ingroup OB
 * @brief Method KE_OB_LookupObjectWithRoot
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KObject>,
    KE_OB_LookupObjectWithRoot,
    const kernel::KObjectPtr<kernel::KObject> &root, const kstd::string_view &path);

/**
 * @ingroup OB
 * @brief Method KE_OB_FindDirectChild
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KObject>,
    KE_OB_FindDirectChild,
    const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kstd::string_view &name);

// region Wellknown directories
/**
 * @ingroup OB
 * @brief Method KE_OB_GetRootDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetRootDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetThreadsDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetThreadsDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetDriverDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetDriverDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetProcessDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetProcessDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetDeviceDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetDeviceDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetMemoryDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetMemoryDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetVFSDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetVFSDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetFileRoot
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetFileDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetFilesystemRoot
 *  
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetFilesystemDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetSubsystemDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetSubsystemDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetModuleDirectory
 *
 * The loaded kernel modules (Kernel, HAL and every loadable subsystem image).
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetModuleDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetConfigRoot
 * 
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetConfigDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetGfxRoot
 * 
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KDirectoryObject>, KE_OB_GetGfxDirectory);
// endregion

// region Helpers
template<typename T, typename Fn, typename... Args>
kernel::KObjectPtr<T> KE_OB_OfType(Fn fn, Args &&...args) noexcept {
  static_assert(kstd::is_invocable_r_v<kernel::KObjectPtr<kernel::KObject>, Fn, Args...>,
                "fn must return KObjectPtr<KObject>");

  if (auto obj = fn(kstd::forward<Args>(args)...)) {
    if (obj->type == T::Type)
      return kernel::KObjectPtr<T>(static_cast<T *>(obj.release()));
  }

  return nullptr;
}

template<typename T>
kernel::KObjectPtr<T> KE_OB_LookupObjectOfType(const kstd::string_view &path) noexcept {
  return KE_OB_OfType<T>(KE_OB_LookupObject, path);
}

template<typename T>
kernel::KObjectPtr<T> KE_OB_LookupObjectOfTypeWithRoot(const kernel::KObjectPtr<kernel::KObject> &root, const kstd::string_view &path) noexcept {
  return KE_OB_OfType<T>(KE_OB_LookupObjectWithRoot, root, path);
}

template<typename T>
kernel::KObjectPtr<T> KE_OB_FindDirectChildOfType(const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kstd::string_view &name) noexcept {
  return KE_OB_OfType<T>(KE_OB_FindDirectChild, parent, name);
}
// endregion
