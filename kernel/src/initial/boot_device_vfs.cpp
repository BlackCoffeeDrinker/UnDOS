#include "boot_device_vfs.hpp"

#include "mini_fat16.hpp"

namespace {
// Per-vnode state: the relative path accumulated across Lookup calls, and
// (after CreateHandle) the full file contents read from the boot FAT16 volume.
struct FS_NodeExtension {
  kstd::static_string<255> path;
  kstd::vector<uint8_t> data;
  bool loaded = false;
};

bool BootDeviceVFS_MountVolume(const kernel::KObjectPtr<kernel::KVolumeMountObject> &volume, const kstd::string_view &options) {
  // We can only support BootDeviceVolume volumes
  if (volume->volume->Name() != "BootDeviceVolume") {
    KE_Print("Invalid volume type for BootDeviceVolume file handle");
    return false;
  }

  // No per-volume state: minifat::ReadFile opens the BIOS boot disk on each
  // call and is fully self-contained.
  (void) options;
  return true;
}

bool BootDeviceVFS_UnmountVolume(const kernel::KObjectPtr<kernel::KVolumeMountObject> &) {
  // No per-volume state to tear down: minifat::ReadFile is fully
  // self-contained per call, so unmounting is always trivially successful.
  return true;
}

bool BootDeviceVFS_GetRootNode(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, kernel::KVFSNode &rootOut) {
  (void) mountPoint;

  rootOut.type = kernel::VFSNodeType::Directory;
  rootOut.fsPrivate = kernel::DataBuffer::Create<FS_NodeExtension>();
  rootOut.size = 0;

  return true;
}

bool BootDeviceVFS_Lookup(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint,
                          const kernel::KVFSNode &parent,
                          kstd::string_view name,
                          kernel::KVFSNode &out) {
  (void) mountPoint;

  // Optimistic Lookup: VFS walks one path component at a time and does not
  // tell the driver whether `name` is intermediate or final. Intermediate
  // components must report Directory or path traversal fails, so every
  // Lookup succeeds as a Directory and only accumulates the relative path.
  // Real existence/type validation happens once in CreateHandle via
  // minifat::ReadFile on the fully built path.
  out.fsPrivate = kernel::DataBuffer::Create<FS_NodeExtension>();
  auto *ctx = out.fsPrivate.as<FS_NodeExtension>();

  if (const auto *parentCtx = parent.fsPrivate.as<FS_NodeExtension>()) {
    if (!parentCtx->path.empty()) {
      ctx->path = parentCtx->path;
      ctx->path.append('/');
    }
  }
  ctx->path.append(name);

  out.type = kernel::VFSNodeType::Directory;
  out.size = 0;
  return true;
}

bool BootDeviceVFS_CreateHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const kernel::KVFSNode &vnode, kernel::KIOObject::OpenMode mode) {
  (void) mountPoint;

  auto *ctx = vnode.fsPrivate.as<FS_NodeExtension>();
  if (!ctx) {
    KE_Print("Invalid node for BootDeviceVolume file handle");
    return false;
  }

  // BootDeviceVolume is physically read-only (no WriteHandle), but the
  // configuration manager opens hives with ReadWrite|Create so that the same
  // path works once rebound onto the real writable root. Accept any mode that
  // includes Read; writes simply fail later via the missing WriteHandle.
  if (!(mode & kernel::KIOObject::OpenMode::Read)) {
    KE_Print("Invalid open mode for BootDeviceVolume file handle");
    return false;
  }

  if (!ctx->loaded) {
    if (!kernel::initial::minifat::ReadFile(ctx->path, ctx->data)) {
      KE_Print("BootDeviceVolume: failed to read '{}'", static_cast<kstd::string_view>(ctx->path));
      return false;
    }
    ctx->loaded = true;
  }

  return true;
}

uint64_t BootDeviceVFS_ReadHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &, const kernel::KVFSNode &vnode, uint64_t offset, const kstd::span<uint8_t> &buffer) {
  const auto *ctx = vnode.fsPrivate.as<FS_NodeExtension>();
  if (!ctx || !ctx->loaded) {
    return 0;
  }

  if (offset >= ctx->data.size()) {
    return 0;
  }

  size_t to_read = buffer.size();
  if (offset + to_read > ctx->data.size()) {
    to_read = static_cast<size_t>(ctx->data.size() - offset);
  }

  __builtin_memcpy(buffer.data(), ctx->data.data() + static_cast<size_t>(offset), to_read);
  return to_read;
}

}// namespace

namespace kernel::initial {
void init_boot_device_volume() {
  const auto volume = KE_IO_CreateDevice(
      nullptr, 0,
      "BootDeviceVolume", device_type::Volume);

  {
    const auto filesystem = KE_VFS_RegisterFilesystemDriver(nullptr,
                                                            "BootDeviceVolume");

    if (!volume || !filesystem) {
      KE_Print("Failed to create BootDeviceVolume or filesystem driver");
      return;
    }

    filesystem->MountVolume = BootDeviceVFS_MountVolume;
    filesystem->UnmountVolume = BootDeviceVFS_UnmountVolume;
    filesystem->GetRootNode = BootDeviceVFS_GetRootNode;
    filesystem->Lookup = BootDeviceVFS_Lookup;
    filesystem->CreateHandle = BootDeviceVFS_CreateHandle;
    filesystem->ReadHandle = BootDeviceVFS_ReadHandle;
  }


  KE_VFS_Mount(volume, "BootDeviceVolume", "/", {});
}

}// namespace kernel::initial
