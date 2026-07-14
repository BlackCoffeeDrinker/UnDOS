#include <kernel/object_manager.hpp>
#include <kernel/vfs.hpp>

#include "stdkrn.hpp"

namespace {
uint64_t opened;
}

namespace kernel::vfs {
void init() {
  opened = 0;
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
    volumemount->fileSystem = kstd::move(fs);
    volumemount->volume = volume;
    volumemount->isMounted = false;

    early_print_fmt("KE_VFS_Mount: Attempting to mount volume '{}' on mount point '{}' with file system '{}'\r\n", volume->Name(), mountPoint, volumemount->fileSystem->name);
    if (volumemount->fileSystem->MountVolume(volumemount, options)) {
      volumemount->isMounted = true;
      early_print_fmt("KE_VFS_Mount: Mounted volume '{}' on mount point '{}' with file system '{}'\r\n", volume->Name(), mountPoint, volumemount->fileSystem->name);
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
  // Create file object
  kstd::static_string<64> fileObjectName;
  kstd::format(fileObjectName, "{}", opened++);

  auto file = kernel::CreateKObject<kernel::KFileObject>(fileObjectName);
  file->mode = mode;
  file->offset = 0;
  file->flags = 0;

  file->mountPoint = findMountPoint(path);
  if (!file->mountPoint) {
    early_print_fmt("KE_VFS_OpenFile: Failed to find mount point for file: {}\r\n", path);
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
    early_print_fmt("KE_VFS_OpenFile: mount {} has no filesystem\r\n", file->mountPoint->name);
    return {};
  }

  // Ensure required FS callbacks exist
  if (!fsObj->Lookup || !fsObj->CreateHandle || !fsObj->GetRootNode) {
    early_print_fmt("KE_VFS_OpenFile: Filesystem missing Lookup/CreateHandle/GetRootNode\r\n");
    return {};
  }

  // Allocate owned node for the file; populate with root node
  file->vfsNode = kstd::make_unique<kernel::KVFSNode>();
  if (!file->FileSystem()->GetRootNode(file->mountPoint, *file->vfsNode)) {
    early_print_fmt("KE_VFS_OpenFile: Failed to get root node for mount {}\r\n", file->mountPoint->name);
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
      early_print_fmt("KE_VFS_OpenFile: '..' component in path '{}'\r\n", path);
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
      early_print_fmt("KE_VFS_OpenFile: Lookup failed for '{}' in '{}'\r\n",
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
      early_print_fmt("KE_VFS_OpenFile: '{}' is not a directory in path '{}'\r\n",
                      name, path);
      return {};
    }

    // loop continues; next iteration will swap again so current child becomes parent
  }

  // ------------------------------------------------------------------
  // Create low-level filesystem handle for this vnode
  // ------------------------------------------------------------------
  if (!fsObj->CreateHandle(file->mountPoint, *file->vfsNode, file->mode)) {
    early_print_fmt("KE_VFS_OpenFile: Filesystem CreateHandle failed for '{}'\r\n", path);
    return {};
  }

  // ------------------------------------------------------------------
  // Insert into Object Manager
  // ------------------------------------------------------------------
  if (!KE_OB_InsertObject(KE_OB_GetFileRoot(), file)) {
    early_print_fmt("KE_VFS_OpenFile: Failed to insert file object for '{}'\r\n", path);
    return {};
  }

  return file;
}

UNDOS_KERNEL_API_DEF void KE_VFS_CloseFile(kernel::KObjectPtr<kernel::KFileObject> &&file) noexcept {
  if (!file) [[unlikely]] { return; }
  if (!file->FileSystem()) [[unlikely]] { return; }
  if (file->FileSystem()->CloseHandle) {
    file->FileSystem()->CloseHandle(file);
  }

  // Remove file from the OB
  KE_OB_RemoveObject(KE_OB_GetFileRoot(), file);
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_ReadFile(kernel::KObjectPtr<kernel::KFileObject> file, kstd::span<uint8_t> &buffer) noexcept {
  if (!file) [[unlikely]] { return 0; }
  if (!file->FileSystem()) [[unlikely]] { return 0; }
  if (!file->FileSystem()->ReadHandle) [[unlikely]] { return 0; }

  const auto read = file->FileSystem()->ReadHandle(file->mountPoint, *file->vfsNode, file->offset, buffer);
  file->offset += read;

  return read;
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_WriteFile(kernel::KObjectPtr<kernel::KFileObject> file, const kstd::span<uint8_t> &buffer) noexcept {
  if (!file) [[unlikely]] { return 0; }
  if (!file->FileSystem()) [[unlikely]] { return 0; }
  if (!file->FileSystem()->WriteHandle) [[unlikely]] { return 0; }

  const auto written = file->FileSystem()->WriteHandle(file->mountPoint, *file->vfsNode, file->offset, buffer);
  file->offset += written;

  return written;
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_SeekFile(kernel::KObjectPtr<kernel::KFileObject> file, uint64_t absolute_offset) noexcept {
  if (!file) [[unlikely]] { return 0; }

  return file->offset = absolute_offset;
}

UNDOS_KERNEL_API_DEF uint64_t KE_VFS_TellFile(const kernel::KObjectPtr<kernel::KFileObject> &file) noexcept {
  if (!file) [[unlikely]] { return 0; }
  return file->offset;
}
