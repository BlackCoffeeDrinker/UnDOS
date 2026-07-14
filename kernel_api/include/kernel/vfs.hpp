
#pragma once
#include <kernel/KObject.hpp>
#include <kernel/vfs/vfs_node.hpp>

/**
 * @ingroup VFS
 * @brief Method KE_VFS_RegisterFilesystemDriver
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KFilesystemObject>,
    KE_VFS_RegisterFilesystemDriver,
    kernel::KObjectPtr<kernel::KDriverObject> driver, const kstd::string_view &name);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_UnregisterFilesystemDriver
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_VFS_UnregisterFilesystemDriver,
    kernel::KObjectPtr<kernel::KFilesystemObject> filesystem);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_Mount
 * 
 * @param volume the volume to mount
 * @param filesystem the filesystem to mount with
 * @param mountPoint where to mount the filesystem
 * @param options options to pass to the filesystem
 * @return true if mount was successful
 */
UNDOS_KERNEL_PUBLIC_V1API(
  bool,
  KE_VFS_Mount,
  const kernel::KDevicePtr<kernel::KDevice> &volume,
  const kstd::string_view &filesystem,
  const kstd::string_view &mountPoint,
  const kstd::string_view &options);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_OpenFile
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KFileObject>,
    KE_VFS_OpenFile,
    const kstd::string_view &path, kernel::KFileObject::OpenMode mode);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_CloseFile
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_VFS_CloseFile,
    kernel::KObjectPtr<kernel::KFileObject> &&file);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_ReadFile
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    uint64_t,
    KE_VFS_ReadFile,
    kernel::KObjectPtr<kernel::KFileObject> file, kstd::span<uint8_t> &buffer);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_WriteFile
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    uint64_t,
    KE_VFS_WriteFile,
    kernel::KObjectPtr<kernel::KFileObject> file, const kstd::span<uint8_t> &buffer);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_SeekFile
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    uint64_t,
    KE_VFS_SeekFile,
    kernel::KObjectPtr<kernel::KFileObject> file, uint64_t absolute_offset);

UNDOS_KERNEL_PUBLIC_V1API(
  uint64_t,
  KE_VFS_TellFile,
  const kernel::KObjectPtr<kernel::KFileObject>& file);
