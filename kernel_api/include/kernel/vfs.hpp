
#pragma once
#include <expected.hpp>
#include <kernel/KObject.hpp>
#include <kernel/kerror.hpp>
#include <kernel/vfs/vfs_node.hpp>

namespace kernel {
using KFilesystemResult = kstd::expected<kernel::KObjectPtr<kernel::KFilesystemObject>, KernelError>;
using KIOObjectResult = kstd::expected<kernel::KObjectPtr<kernel::KIOObject>, KernelError>;
using KUInt64Result = kstd::expected<uint64_t, KernelError>;
}// namespace kernel


/**
 * @ingroup VFS
 * @brief Method KE_VFS_RegisterFilesystemDriver
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KFilesystemResult,
    KE_VFS_RegisterFilesystemDriver,
    kernel::KObjectPtr<kernel::KDriverObject> driver, const kstd::string_view &name);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_UnregisterFilesystemDriver
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
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
 * @return Success, or the reason the mount failed (AlreadyExists, NotFound, filesystem error...)
 */
UNDOS_KERNEL_PUBLIC_V1API(
  kernel::KernelError,
  KE_VFS_Mount,
  const kernel::KDevicePtr<kernel::KDevice> &volume,
  const kstd::string_view &filesystem,
  const kstd::string_view &mountPoint,
  const kstd::string_view &options);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_Unmount
 *
 * @param mountPoint the exact mount point to unmount
 * @return Success if a mount existed at that exact path and was unmounted, NotFound otherwise
 */
UNDOS_KERNEL_PUBLIC_V1API(
  kernel::KernelError,
  KE_VFS_Unmount,
  const kstd::string_view &mountPoint);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_OpenFile
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KIOObjectResult,
    KE_VFS_OpenFile,
    const kstd::string_view &path, kernel::KIOObject::OpenMode mode);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_CloseFile
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_VFS_CloseFile,
    const kernel::KObjectPtr<kernel::KIOObject> &file);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_ReadFile
 *
 * Returns the bytes read; success with 0 bytes means end of file.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KUInt64Result,
    KE_VFS_ReadFile,
    const kernel::KObjectPtr<kernel::KIOObject> &file, const kstd::span<uint8_t> &readInto);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_WriteFile
 *
 * Returns the bytes written, which may be fewer than requested.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KUInt64Result,
    KE_VFS_WriteFile,
    const kernel::KObjectPtr<kernel::KIOObject> &file, const kstd::span<const uint8_t> &buffer);

/**
 * @ingroup VFS
 * @brief Method KE_VFS_SeekFile
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KUInt64Result,
    KE_VFS_SeekFile,
    const kernel::KObjectPtr<kernel::KIOObject> &file, uint64_t absolute_offset);

UNDOS_KERNEL_PUBLIC_V1API(
  kernel::KUInt64Result,
  KE_VFS_TellFile,
  const kernel::KObjectPtr<kernel::KIOObject>& file);
