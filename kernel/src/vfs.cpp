#include <kernel/object_manager.hpp>
#include <kernel/vfs.hpp>

#include "stdkrn.hpp"

namespace {
// Object manager insertion/removal report success either as bool or KernelError.
}

namespace kernel {
namespace vfs {
void init() {
}
}// namespace vfs
}// namespace kernel

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

UNDOS_KERNEL_API_DEF kernel::KernelError KE_VFS_Mount(const kernel::KDevicePtr<kernel::KDevice> &volume,
                                       const kstd::string_view &filesystem,
                                       const kstd::string_view &mountPoint,
                                       const kstd::string_view &options) noexcept {
  // Can we mount at `mountPoint`?
  const auto existing = findMountPoint(mountPoint);

  if (existing && existing->name == mountPoint) {
    KE_Print("KE_VFS_Mount: Mount point '{}' already in use", mountPoint);
    return kernel::KernelError::AlreadyExists;
  }

  // Only "/" can be mounted if no parent mount exists
  if (!existing && !mountPointIsSpecial(mountPoint)) {
    KE_Print("KE_VFS_Mount: Mount point '{}' is not valid", mountPoint);
    return kernel::KernelError::InvalidArgument;
  }

  // Find file system
  if (auto fs = KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(KE_OB_GetFilesystemDirectory(), filesystem)) {
    KE_Print("KE_VFS_Mount: Found filesystem driver with name '{}'", filesystem);
    auto created = kernel::CreateKObject<kernel::KVolumeMountObject>(mountPoint);
    if (!created) return created.error();
    const auto volumemount = kstd::move(*created);
    volumemount->fileSystem = kstd::move(fs);
    volumemount->volume = volume;
    volumemount->isMounted = false;

    KE_Print("KE_VFS_Mount: Attempting to mount volume '{}' on mount point '{}' with file system '{}'", volume->Name(), mountPoint, volumemount->fileSystem->name);
    if (!volumemount->fileSystem->MountVolume) {
      KE_Print("KE_VFS_Mount: Filesystem '{}' has no MountVolume handler", filesystem);
      return kernel::KernelError::Unsupported;
    }
    if (const auto status = volumemount->fileSystem->MountVolume(volumemount, options); status != kernel::KernelError::Success) {
      KE_Print("KE_VFS_Mount: Failed to mount volume");
      return status;
    }

    volumemount->isMounted = true;
    KE_Print("KE_VFS_Mount: Mounted volume '{}' on mount point '{}' with file system '{}'", volume->Name(), mountPoint, volumemount->fileSystem->name);
    if (const auto status = KE_OB_InsertObject(KE_OB_GetVFSDirectory(), volumemount); status != kernel::KernelError::Success) {
      volumemount->isMounted = false;
      if (volumemount->fileSystem->UnmountVolume) volumemount->fileSystem->UnmountVolume(volumemount);
      KE_Print("KE_VFS_Mount: Failed to publish mount point '{}'", mountPoint);
      return status;
    }
    return kernel::KernelError::Success;
  }

  KE_Print("KE_VFS_Mount: Filesystem driver with name '{}' not found", filesystem);
  return kernel::KernelError::NotFound;
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_VFS_Unmount(const kstd::string_view &mountPoint) noexcept {
  const auto vfsRoot = KE_OB_GetVFSDirectory();
  if (!vfsRoot) return kernel::KernelError::NotInitialized;

  const auto mount = KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(vfsRoot, mountPoint);
  if (!mount || mount->name != mountPoint) {
    KE_Print("KE_VFS_Unmount: Mount point '{}' not found", mountPoint);
    return kernel::KernelError::NotFound;
  }

  if (mount->fileSystem && mount->fileSystem->UnmountVolume) {
    if (const auto status = mount->fileSystem->UnmountVolume(mount); status != kernel::KernelError::Success) {
      KE_Print("KE_VFS_Unmount: Filesystem refused to unmount '{}'", mountPoint);
      return status;
    }
  }

  mount->isMounted = false;
  if (const auto status = KE_OB_RemoveObject(vfsRoot, mount); status != kernel::KernelError::Success) return status;
  KE_Print("KE_VFS_Unmount: Unmounted '{}'", mountPoint);
  return kernel::KernelError::Success;
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KFilesystemObject>, kernel::KernelError> KE_VFS_RegisterFilesystemDriver(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    const kstd::string_view &name) noexcept {

  // Make sure the name is unique
  if (auto fsko = KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(KE_OB_GetFilesystemDirectory(), name)) {
    KE_Print("KE_VFS_RegisterFilesystemDriver: Filesystem driver with name '{}' already exists", name);
    return kstd::unexpected(kernel::KernelError::AlreadyExists);
  }

  auto created = kernel::CreateKObject<kernel::KFilesystemObject>(name);
  if (!created) return kstd::unexpected(created.error());
  auto fsko = kstd::move(*created);
  fsko->driver = kstd::move(driver);
  if (const auto status = KE_OB_InsertObject(KE_OB_GetFilesystemDirectory(), fsko); status != kernel::KernelError::Success) {
    return kstd::unexpected(status);
  }
  KE_Print("KE_VFS_RegisterFilesystemDriver: Registered filesystem driver '{}'", name);
  return fsko;
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_VFS_UnregisterFilesystemDriver(kernel::KObjectPtr<kernel::KFilesystemObject> filesystem) noexcept {
  (void) filesystem;
  return kernel::KernelError::NotImplemented;
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KIOObject>, kernel::KernelError> KE_VFS_OpenFile(const kstd::string_view &path, kernel::KIOObject::OpenMode mode) noexcept {
  // Create a uniquely-named file object. Name is const on KObject, so it must
  // be chosen before construction: allocate, format the storage address as
  // hex (the previous intent of the unused fileObjectName local), then
  // placement-new. An empty name collides on the second open under FileRoot.
  void *storage = KE_Malloc(sizeof(kernel::KIOObject));
  if (!storage) {
    KE_Print("KE_VFS_OpenFile: Failed to allocate file object for '{}'", path);
    return kstd::unexpected(kernel::KernelError::OutOfMemory);
  }
  kstd::static_string<64> fileObjectName;
  kstd::format(fileObjectName, "{:x}", storage);
  auto file = kernel::KObjectPtr(new (storage) kernel::KIOObject(fileObjectName));

  file->ops = &kernel::io::FileOps;

  file->mode = mode;
  file->offset = 0;
  file->flags = 0;

  file->mountPoint = findMountPoint(path);
  if (!file->mountPoint) {
    KE_Print("KE_VFS_OpenFile: Failed to find mount point for file: {}", path);
    return kstd::unexpected(kernel::KernelError::NotFound);
  }

  // Strip mount point prefix
  auto rel = path.substr(file->mountPoint->name.size());
  if (!rel.empty() && (rel[0] == '/')) {
    rel = rel.substr(1);
  }

  file->actualPath = rel;

  // Filesystem pointer convenience
  const auto &fsObj = file->FileSystem();
  if (!fsObj) {
    KE_Print("KE_VFS_OpenFile: mount {} has no filesystem", file->mountPoint->name);
    return kstd::unexpected(kernel::KernelError::InvalidState);
  }

  // Ensure required FS callbacks exist
  if (!fsObj->Lookup || !fsObj->CreateHandle || !fsObj->GetRootNode) {
    KE_Print("KE_VFS_OpenFile: Filesystem missing Lookup/CreateHandle/GetRootNode");
    return kstd::unexpected(kernel::KernelError::Unsupported);
  }

  // Allocate owned node for the file; populate with root node
  file->vfsNode = kstd::make_unique<kernel::KVFSNode>();
  if (!file->vfsNode) return kstd::unexpected(kernel::KernelError::OutOfMemory);
  if (const auto status = file->FileSystem()->GetRootNode(file->mountPoint, *file->vfsNode); status != kernel::KernelError::Success) {
    KE_Print("KE_VFS_OpenFile: Failed to get root node for mount {}", file->mountPoint->name);
    return kstd::unexpected(status);
  }

  // Temporary node reused each iteration; swap pattern avoids extra allocations
  auto tmpnode = kstd::make_unique<kernel::KVFSNode>();
  if (!tmpnode) return kstd::unexpected(kernel::KernelError::OutOfMemory);

  // ----------------------------------------------------------------------
  // Path traversal (allocation-free, string_view slicing)
  // ----------------------------------------------------------------------
  size_t pos = 0;
  while (pos < rel.size()) {
    // Find next separator
    auto sep = rel.find('/', pos);
    if (sep == kstd::string_view::npos) {
      sep = rel.size();
    }

    // Extract component
    const auto name = rel.substr(pos, sep - pos);
    pos = sep + 1;

    if (name.empty() || name == ".") {
      continue;
    }

    if (name == "..") {
      KE_Print("KE_VFS_OpenFile: '..' component in path '{}'", path);
      return kstd::unexpected(kernel::KernelError::InvalidArgument);
    }

    // --------------------------------------------------------------
    // CACHING HOOK:
    //   Check lookup/dentry cache for (parentId, name)
    //   If found, set file->vfsNode = cached and continue.
    // --------------------------------------------------------------
    // Example placeholder:
    // auto cached = KE_VFS_LookupCache(parentId, name);
    // if (cached) { file->vfsNode = std::move(cached); continue; }

    // --------------------------------------------------------------
    // Perform lookup
    // --------------------------------------------------------------
    // swap so tmpnode holds the parent (initially root), file->vfsNode becomes empty
    kstd::swap(file->vfsNode, tmpnode);

    // tmpnode now contains the parent node (borrowed copy we own)
    // file->vfsNode is the output storage for the child
    if (const auto status = fsObj->Lookup(file->mountPoint, *tmpnode, name, *file->vfsNode); status != kernel::KernelError::Success) {
      KE_Print("KE_VFS_OpenFile: Lookup failed for '{}' in '{}'",
                      name, rel);
      return kstd::unexpected(status);
    }

    // --------------------------------------------------------------
    // PERMISSION CHECK HOOK:
    //   - For intermediate components: search/execute permission
    //   - For last component: read/write/execute based on mode
    // --------------------------------------------------------------
    // if (!KE_VFS_CheckPermissions(*file->vfsNode, mode, isLast)) return {};

    // --------------------------------------------------------------
    // CACHING HOOK:
    //   Insert (parentId, name) -> file->vfsNode into cache
    // --------------------------------------------------------------
    // KE_VFS_InsertCache(parentId, name, *file->vfsNode);

    // Validate directory for intermediate components
    const auto isLast = pos >= rel.size();
    if (!isLast && file->vfsNode->type != kernel::VFSNodeType::Directory) {
      KE_Print("KE_VFS_OpenFile: '{}' is not a directory in path '{}'",
                      name, path);
      return kstd::unexpected(kernel::KernelError::NotDirectory);
    }

    // loop continues; next iteration will swap again so current child becomes parent
  }

  // ------------------------------------------------------------------
  // Create low-level filesystem handle for this vnode
  // ------------------------------------------------------------------
  if (const auto status = fsObj->CreateHandle(file->mountPoint, *file->vfsNode, file->mode); status != kernel::KernelError::Success) {
    KE_Print("KE_VFS_OpenFile: Filesystem CreateHandle failed for '{}'", path);
    return kstd::unexpected(status);
  }
  file->fileHandleCreated = true;

  // ------------------------------------------------------------------
  // Insert into Object Manager
  // ------------------------------------------------------------------
  if (const auto status = KE_OB_InsertObject(KE_OB_GetFileDirectory(), file); status != kernel::KernelError::Success) {
    KE_Print("KE_VFS_OpenFile: Failed to insert file object for '{}'", path);
    KE_VFS_CloseFile(file);
    return kstd::unexpected(status);
  }

  return file;
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_VFS_CloseFile(const kernel::KObjectPtr<kernel::KIOObject> &file) noexcept {
  if (!file) [[unlikely]] { return kernel::KernelError::InvalidHandle; }
  if (file->isClosed) [[unlikely]] { return kernel::KernelError::Success; }
  if (file->ops != &kernel::io::FileOps) return kernel::KernelError::WrongObjectType;
  file->isClosed = true;
  if (file->fileHandleCreated && file->mountPoint && file->FileSystem() && file->FileSystem()->CloseHandle) {
    file->FileSystem()->CloseHandle(file);
  }
  file->fileHandleCreated = false;
  file->vfsNode = nullptr;

  // Remove file from the OB
  if (file->parent) return KE_OB_RemoveObject(KE_OB_GetFileDirectory(), file);
  return kernel::KernelError::Success;
}

UNDOS_KERNEL_API_DEF kstd::expected<uint64_t, kernel::KernelError> KE_VFS_ReadFile(const kernel::KObjectPtr<kernel::KIOObject> &file, const kstd::span<uint8_t> &readInto) noexcept {
  if (!file) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidHandle); }
  if (file->ops != &kernel::io::FileOps) [[unlikely]] { return kstd::unexpected(kernel::KernelError::WrongObjectType); }
  if (file->isClosed || !file->vfsNode) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidState); }
  if (!file->FileSystem()) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidState); }
  if (!file->FileSystem()->ReadHandle) [[unlikely]] { return kstd::unexpected(kernel::KernelError::Unsupported); }

  // Make sure the mode includes read
  if (!(file->mode & kernel::KIOObject::OpenMode::Read)) [[unlikely]] { return kstd::unexpected(kernel::KernelError::AccessDenied); }

  const auto read = file->FileSystem()->ReadHandle(file->mountPoint, *file->vfsNode, file->offset, readInto);
  if (!read) return read;
  file->offset += *read;

  return read;
}

UNDOS_KERNEL_API_DEF kstd::expected<uint64_t, kernel::KernelError> KE_VFS_WriteFile(const kernel::KObjectPtr<kernel::KIOObject> &file, const kstd::span<const uint8_t> &buffer) noexcept {
  if (!file) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidHandle); }
  if (file->ops != &kernel::io::FileOps) [[unlikely]] { return kstd::unexpected(kernel::KernelError::WrongObjectType); }
  if (file->isClosed || !file->vfsNode) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidState); }
  if (!file->FileSystem()) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidState); }
  if (!file->FileSystem()->WriteHandle) [[unlikely]] { return kstd::unexpected(kernel::KernelError::Unsupported); }

  if (!(file->mode & kernel::KIOObject::OpenMode::Write)) [[unlikely]] { return kstd::unexpected(kernel::KernelError::AccessDenied); }

  const auto written = file->FileSystem()->WriteHandle(file->mountPoint, *file->vfsNode, file->offset, buffer);
  if (!written) return written;
  file->offset += *written;

  return written;
}

UNDOS_KERNEL_API_DEF kstd::expected<uint64_t, kernel::KernelError> KE_VFS_SeekFile(const kernel::KObjectPtr<kernel::KIOObject> &file, uint64_t absolute_offset) noexcept {
  if (!file) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidHandle); }
  if (file->isClosed) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidState); }

  return file->offset = absolute_offset;
}

UNDOS_KERNEL_API_DEF kstd::expected<uint64_t, kernel::KernelError> KE_VFS_TellFile(const kernel::KObjectPtr<kernel::KIOObject> &file) noexcept {
  if (!file) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidHandle); }
  return file->offset;
}

namespace kernel {
namespace {
KernelError FileRead(KIOObject &io, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (!(io.mode & KIOObject::OpenMode::Read)) return KernelError::AccessDenied;
  transferred = 0;
  const auto read = KE_VFS_ReadFile(&io, buffer);
  if (!read) return read.error();
  transferred = static_cast<size_t>(*read);
  return KernelError::Success;
}

KernelError FileWrite(KIOObject &io, kstd::span<const uint8_t> buffer, size_t &transferred) {
  if (!(io.mode & KIOObject::OpenMode::Write)) return KernelError::AccessDenied;
  transferred = 0;
  const auto written = KE_VFS_WriteFile(&io, buffer);
  if (!written) return written.error();
  transferred = static_cast<size_t>(*written);
  return KernelError::Success;
}

KernelError FileClose(KIOObject &io) {
  return KE_VFS_CloseFile(&io);
}
} // namespace

namespace io {
const KIOOps FileOps{.Read = FileRead, .Write = FileWrite, .Control = {}, .Close = FileClose};
} // namespace io
} // namespace kernel
