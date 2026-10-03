#include <kernel/object_manager.hpp>
#include <kernel/vfs.hpp>

#include "stdkrn.hpp"

namespace {
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

UNDOS_KERNEL_API_DEF bool KE_VFS_Mount(const kernel::KDevicePtr<kernel::KDevice> &volume,
                                       const kstd::string_view &filesystem,
                                       const kstd::string_view &mountPoint,
                                       const kstd::string_view &options) noexcept {
  // Can we mount at `mountPoint`?
  const auto existing = findMountPoint(mountPoint);

  if (existing && existing->name == mountPoint) {
    KE_Print("KE_VFS_Mount: Mount point '{}' already in use", mountPoint);
    return false;
  }

  // Only "/" can be mounted if no parent mount exists
  if (!existing && !mountPointIsSpecial(mountPoint)) {
    KE_Print("KE_VFS_Mount: Mount point '{}' is not valid", mountPoint);
    return false;
  }

  // Find file system
  if (auto fs = KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(KE_OB_GetFilesystemDirectory(), filesystem)) {
    KE_Print("KE_VFS_Mount: Found filesystem driver with name '{}'", filesystem);
    const auto volumemount = kernel::CreateKObject<kernel::KVolumeMountObject>(mountPoint);
    volumemount->fileSystem = kstd::move(fs);
    volumemount->volume = volume;
    volumemount->isMounted = false;

    KE_Print("KE_VFS_Mount: Attempting to mount volume '{}' on mount point '{}' with file system '{}'", volume->Name(), mountPoint, volumemount->fileSystem->name);
    if (volumemount->fileSystem->MountVolume(volumemount, options)) {
      volumemount->isMounted = true;
      KE_Print("KE_VFS_Mount: Mounted volume '{}' on mount point '{}' with file system '{}'", volume->Name(), mountPoint, volumemount->fileSystem->name);
      KE_OB_InsertObject(KE_OB_GetVFSDirectory(), volumemount);
      return true;
    }

    KE_Print("KE_VFS_Mount: Failed to mount volume");
  }

  KE_Print("KE_VFS_Mount: Filesystem driver with name '{}' not found", filesystem);
  return false;
}

UNDOS_KERNEL_API_DEF bool KE_VFS_Unmount(const kstd::string_view &mountPoint) noexcept {
  const auto vfsRoot = KE_OB_GetVFSDirectory();
  if (!vfsRoot) return false;

  const auto mount = KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(vfsRoot, mountPoint);
  if (!mount || mount->name != mountPoint) {
    KE_Print("KE_VFS_Unmount: Mount point '{}' not found", mountPoint);
    return false;
  }

  if (mount->fileSystem && mount->fileSystem->UnmountVolume) {
    if (!mount->fileSystem->UnmountVolume(mount)) {
      KE_Print("KE_VFS_Unmount: Filesystem refused to unmount '{}'", mountPoint);
      return false;
    }
  }

  mount->isMounted = false;
  KE_OB_RemoveObject(vfsRoot, mount);
  KE_Print("KE_VFS_Unmount: Unmounted '{}'", mountPoint);
  return true;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KFilesystemObject> KE_VFS_RegisterFilesystemDriver(
    kernel::KObjectPtr<kernel::KDriverObject> driver,
    const kstd::string_view &name) noexcept {

  // Make sure the name is unique
  if (auto fsko = KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(KE_OB_GetFilesystemDirectory(), name)) {
    KE_Print("KE_VFS_RegisterFilesystemDriver: Filesystem driver with name '{}' already exists", name);
    return {};
  }

  if (auto fsko = kernel::CreateKObject<kernel::KFilesystemObject>(name)) {
    fsko->driver = kstd::move(driver);
    KE_OB_InsertObject(KE_OB_GetFilesystemDirectory(), fsko);
    KE_Print("KE_VFS_RegisterFilesystemDriver: Registered filesystem driver '{}'", name);
    return fsko;
  }

  return {};
}

UNDOS_KERNEL_API_DEF void KE_VFS_UnregisterFilesystemDriver(kernel::KObjectPtr<kernel::KFilesystemObject> filesystem) noexcept {
  (void) filesystem;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KIOObject> KE_VFS_OpenFile(const kstd::string_view &path, kernel::KIOObject::OpenMode mode) noexcept {
  // Create a uniquely-named file object. Name is const on KObject, so it must
  // be chosen before construction: allocate, format the storage address as
  // hex (the previous intent of the unused fileObjectName local), then
  // placement-new. An empty name collides on the second open under FileRoot.
  void *storage = KE_Malloc(sizeof(kernel::KIOObject));
  if (!storage) {
    KE_Print("KE_VFS_OpenFile: Failed to allocate file object for '{}'", path);
    return {};
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
    return {};
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
    return {};
  }

  // Ensure required FS callbacks exist
  if (!fsObj->Lookup || !fsObj->CreateHandle || !fsObj->GetRootNode) {
    KE_Print("KE_VFS_OpenFile: Filesystem missing Lookup/CreateHandle/GetRootNode");
    return {};
  }

  // Allocate owned node for the file; populate with root node
  file->vfsNode = kstd::make_unique<kernel::KVFSNode>();
  if (!file->FileSystem()->GetRootNode(file->mountPoint, *file->vfsNode)) {
    KE_Print("KE_VFS_OpenFile: Failed to get root node for mount {}", file->mountPoint->name);
    return {};
  }

  // Temporary node reused each iteration; swap pattern avoids extra allocations
  auto tmpnode = kstd::make_unique<kernel::KVFSNode>();

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
      return {};
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
    if (!fsObj->Lookup(file->mountPoint, *tmpnode, name, *file->vfsNode)) {
      KE_Print("KE_VFS_OpenFile: Lookup failed for '{}' in '{}'",
                      name, rel);
      return {};
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
      return {};
    }

    // loop continues; next iteration will swap again so current child becomes parent
  }

  // ------------------------------------------------------------------
  // Create low-level filesystem handle for this vnode
  // ------------------------------------------------------------------
  if (!fsObj->CreateHandle(file->mountPoint, *file->vfsNode, file->mode)) {
    KE_Print("KE_VFS_OpenFile: Filesystem CreateHandle failed for '{}'", path);
    return {};
  }
  file->fileHandleCreated = true;

  // ------------------------------------------------------------------
  // Insert into Object Manager
  // ------------------------------------------------------------------
  if (!KE_OB_InsertObject(KE_OB_GetFileDirectory(), file)) {
    KE_Print("KE_VFS_OpenFile: Failed to insert file object for '{}'", path);
    KE_VFS_CloseFile(file);
    return {};
  }

  return file;
}

UNDOS_KERNEL_API_DEF void KE_VFS_CloseFile(const kernel::KObjectPtr<kernel::KIOObject> &file) noexcept {
  if (!file) [[unlikely]] { return; }
  if (file->isClosed) [[unlikely]] { return; }
  if (file->ops != &kernel::io::FileOps) return;
  file->isClosed = true;
  if (file->fileHandleCreated && file->mountPoint && file->FileSystem() && file->FileSystem()->CloseHandle) {
    file->FileSystem()->CloseHandle(file);
  }
  file->fileHandleCreated = false;
  file->vfsNode = nullptr;

  // Remove file from the OB
  if (file->parent) KE_OB_RemoveObject(KE_OB_GetFileDirectory(), file);
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_ReadFile(const kernel::KObjectPtr<kernel::KIOObject> &file, const kstd::span<uint8_t> &readInto) noexcept {
  if (!file || file->isClosed || !file->vfsNode || file->ops != &kernel::io::FileOps) [[unlikely]] { return 0; }
  if (!file->FileSystem()) [[unlikely]] { return 0; }
  if (!file->FileSystem()->ReadHandle) [[unlikely]] { return 0; }
  
  // Make sure the mode includes read
  if (!(file->mode & kernel::KIOObject::OpenMode::Read)) [[unlikely]] { return 0; }

  const auto read = file->FileSystem()->ReadHandle(file->mountPoint, *file->vfsNode, file->offset, readInto);
  file->offset += read;

  return read;
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_WriteFile(const kernel::KObjectPtr<kernel::KIOObject> &file, const kstd::span<const uint8_t> &buffer) noexcept {
  if (!file || file->isClosed || !file->vfsNode || file->ops != &kernel::io::FileOps) [[unlikely]] { return 0; }
  if (!file->FileSystem()) [[unlikely]] { return 0; }
  if (!file->FileSystem()->WriteHandle) [[unlikely]] { return 0; }

  if (!(file->mode & kernel::KIOObject::OpenMode::Write)) [[unlikely]] { return 0; }
  
  const auto written = file->FileSystem()->WriteHandle(file->mountPoint, *file->vfsNode, file->offset, buffer);
  file->offset += written;

  return written;
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_SeekFile(const kernel::KObjectPtr<kernel::KIOObject> &file, uint64_t absolute_offset) noexcept {
  if (!file || file->isClosed) [[unlikely]] { return 0; }

  return file->offset = absolute_offset;
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_TellFile(const kernel::KObjectPtr<kernel::KIOObject> &file) noexcept {
  if (!file) [[unlikely]] { return 0; }
  return file->offset;
}

namespace kernel {
namespace {
IoStatus FileRead(KIOObject &io, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (!(io.mode & KIOObject::OpenMode::Read)) return IoStatus::AccessDenied;
  transferred = static_cast<size_t>(KE_VFS_ReadFile(&io, buffer));
  return IoStatus::Success;
}

IoStatus FileWrite(KIOObject &io, kstd::span<const uint8_t> buffer, size_t &transferred) {
  if (!(io.mode & KIOObject::OpenMode::Write)) return IoStatus::AccessDenied;
  transferred = static_cast<size_t>(KE_VFS_WriteFile(&io, buffer));
  return IoStatus::Success;
}

IoStatus FileClose(KIOObject &io) {
  KE_VFS_CloseFile(&io);
  return IoStatus::Success;
}
} // namespace

namespace io {
const KIOOps FileOps{FileRead, FileWrite, {}, FileClose};
} // namespace io
} // namespace kernel
