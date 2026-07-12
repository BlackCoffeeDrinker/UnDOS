
#pragma once
#include <kernel/__core.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/ObjectType.hpp>

#include <string_view.hpp>

namespace kernel {
struct KObject;
struct KDirectoryObject;
}// namespace kernel

/**
 * @ingroup OB
 * @brief Method KE_OB_InsertObject
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_OB_InsertObject,
    const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kernel::KObjectPtr<kernel::KObject> &child);

/**
 * @ingroup OB
 * @brief Method KE_OB_RemoveObject
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    bool,
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
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDirectoryObject>,
    KE_OB_GetRootDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetThreadsDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDirectoryObject>,
    KE_OB_GetThreadsDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetDriverDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDirectoryObject>,
    KE_OB_GetDriverDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetProcessDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDirectoryObject>,
    KE_OB_GetProcessDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetDeviceDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDirectoryObject>,
    KE_OB_GetDeviceDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetMemoryDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDirectoryObject>,
    KE_OB_GetMemoryDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetVFSDirectory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDirectoryObject>,
    KE_OB_GetVFSDirectory);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetFileRoot
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDirectoryObject>,
    KE_OB_GetFileRoot);

/**
 * @ingroup OB
 * @brief Method KE_OB_GetFilesystemRoot
 *  
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDirectoryObject>,
    KE_OB_GetFilesystemRoot);
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
