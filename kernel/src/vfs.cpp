#include <kernel/object_manager.hpp>
#include <kernel/vfs.hpp>

#include "stdkrn.hpp"

namespace kernel::vfs {
void init() {
}
}// namespace kernel::vfs

namespace {
kernel::KObjectPtr<kernel::KVolumeMountObject> findMountPoint(const kstd::string_view &path) {
  const auto vfs_root = KE_OB_GetVFSDirectory();
  if (!vfs_root) return nullptr;

  // Path looks like "/somefolder/someotherfolder/my file.txt"
  // Mount names look like "/", "/somefolder", "/somefolder/someotherfolder"

  kstd::string_view::size_type cut = path.size();

  while (cut > 0) {
    const auto candidate = path.substr(0, cut);

    // Try exact match
    if (auto mount = KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(
            vfs_root, candidate)) {
      return mount;
    }

    // The root mount "/" has just been tried (candidate == "/"); there is
    // nothing shorter left to try.
    if (cut == 1) break;

    // Strip the last path segment. If the previous slash is the leading one,
    // the next candidate is the root mount "/" itself (length 1), not an
    // empty string.
    const auto next = path.rfind('/', cut - 1);
    cut = (next == kstd::string_view::npos || next == 0) ? 1 : next;
  }

  return nullptr;
}
bool mountPointIsSpecial(const kstd::string_view &mount_point) {
  return mount_point == "/" ||
         mount_point.starts_with("//!/") ||
         mount_point.starts_with("//./");
}

}// namespace

UNDOS_KERNEL_API_DEF bool KE_VFS_Mount(const kernel::KDevicePtr<kernel::KDevice> &volume,
                                       const kstd::string_view &filesystem,
                                       const kstd::string_view &mountPoint,
                                       const kstd::string_view &options) noexcept {
  // Can we mount at `mountPoint`?
  const auto existing = findMountPoint(mountPoint);

  if (existing && existing->name == mountPoint) {
    early_print_fmt("KE_VFS_Mount: Mount point '{}' already in use\r\n", mountPoint);
    return false;
  }

  // Only "/" can be mounted if no parent mount exists
  if (!existing && !mountPointIsSpecial(mountPoint)) {
    early_print_fmt("KE_VFS_Mount: Mount point '{}' is not valid\r\n", mountPoint);
    return false;
  }

  // Find file system
  if (auto fs = KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(KE_OB_GetFilesystemRoot(), filesystem)) {
    early_print_fmt("KE_VFS_Mount: Found filesystem driver with name '{}'\r\n", filesystem);
    const auto volumemount = kernel::CreateKObject<kernel::KVolumeMountObject>(mountPoint);
    volumemount->filesystem = kstd::move(fs);
    volumemount->volume = volume;
    volumemount->isMounted = false;

    early_print_fmt("KE_VFS_Mount: Attempting to mount volume '{}' on mount point '{}' with file system '{}'\r\n", volume->Name(), mountPoint, volumemount->filesystem->name);
    if (volumemount->filesystem->MountVolume(volumemount, options)) {
      volumemount->isMounted = true;
      early_print_fmt("KE_VFS_Mount: Mounted volume '{}' on mount point '{}' with file system '{}'\r\n", volume->Name(), mountPoint, volumemount->filesystem->name);
      KE_OB_InsertObject(KE_OB_GetVFSDirectory(), volumemount);
      return true;
    }

    early_print_fmt("KE_VFS_Mount: Failed to mount volume\r\n");
  }

  early_print_fmt("KE_VFS_Mount: Filesystem driver with name '{}' not found\r\n", filesystem);
  return false;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KFilesystemObject> KE_VFS_RegisterFilesystemDriver(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    const kstd::string_view &name) noexcept {

  // Make sure the name is unique
  if (auto fsko = KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(KE_OB_GetFilesystemRoot(), name)) {
    early_print_fmt("KE_VFS_RegisterFilesystemDriver: Filesystem driver with name '{}' already exists\r\n", name);
    return {};
  }

  if (auto fsko = kernel::CreateKObject<kernel::KFilesystemObject>(name)) {
    fsko->driver = kstd::move(driver);
    KE_OB_InsertObject(KE_OB_GetFilesystemRoot(), fsko);
    early_print_fmt("KE_VFS_RegisterFilesystemDriver: Registered filesystem driver '{}'\r\n", name);
    return fsko;
  }

  return {};
}

UNDOS_KERNEL_API_DEF void KE_VFS_UnregisterFilesystemDriver(kernel::KObjectPtr<kernel::KFilesystemObject> filesystem) noexcept {
  (void) filesystem;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KFileObject> KE_VFS_OpenFile(const kstd::string_view &path, kernel::KFileObject::OpenMode mode) noexcept {
  // Special case 1: if path starts with "//!/" then we'll look Directly for a volume name in the
  // So the initial loader would use //!/InitialVFSVolume/bus_isa

  // Special case 2: (not implemented) if path starts with "//./" then we're browsing the IO Manager devices

  if (auto mountPoint = findMountPoint(path)) {
    auto file = kernel::CreateKObject<kernel::KFileObject>({});
    // Remove mountPoint->name from path
    file->actualPath = path.substr(mountPoint->name.size());
    file->mode = mode;
    file->offset = 0;
    file->flags = 0;
    file->mountPoint = kstd::move(mountPoint);

    if (auto fs = file->mountPoint->filesystem) {
      file->fileSystem = kstd::move(fs);
      if (file->fileSystem->CreateHandle(file)) {
        KE_OB_InsertObject(KE_OB_GetFileRoot(), file);
        return file;
      }
    }
  }

  early_print_fmt("KE_VFS_OpenFile: Failed to find mount point for file: {}\r\n", path);

  return {};
}

UNDOS_KERNEL_API_DEF void KE_VFS_CloseFile(kernel::KObjectPtr<kernel::KFileObject> &&file) noexcept {
  if (!file) [[unlikely]] { return; }
  if (!file->fileSystem) [[unlikely]] { return; }
  if (file->fileSystem->CloseHandle) {
    file->fileSystem->CloseHandle(file);
  }

  // Remove file from the OB
  KE_OB_RemoveObject(KE_OB_GetFileRoot(), file);
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_ReadFile(kernel::KObjectPtr<kernel::KFileObject> file, kstd::span<uint8_t> &buffer) noexcept {
  if (!file) [[unlikely]] { return 0; }
  if (!file->fileSystem) [[unlikely]] { return 0; }
  if (!file->fileSystem->ReadHandle) [[unlikely]] { return 0; }

  return file->fileSystem->ReadHandle(file, buffer);
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_WriteFile(kernel::KObjectPtr<kernel::KFileObject> file, const kstd::span<uint8_t> &buffer) noexcept {
  if (!file) [[unlikely]] { return 0; }
  if (!file->fileSystem) [[unlikely]] { return 0; }
  if (!file->fileSystem->WriteHandle) [[unlikely]] { return 0; }

  return file->fileSystem->WriteHandle(file, buffer);
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_SeekFile(kernel::KObjectPtr<kernel::KFileObject> file, uint64_t absolute_offset) noexcept {
  if (!file) [[unlikely]] { return 0; }
  if (!file->fileSystem) [[unlikely]] { return 0; }

  if (file->fileSystem->SeekHandle) {
    return file->fileSystem->SeekHandle(file, absolute_offset);
  }

  return file->offset = absolute_offset;
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_TellFile(const kernel::KObjectPtr<kernel::KFileObject> &file) noexcept {
  if (!file) [[unlikely]] { return 0; }
  return file->offset;
}
