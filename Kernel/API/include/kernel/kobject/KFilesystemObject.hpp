#pragma once

#include <expected.hpp>
#include <memory.hpp>

#include <kernel/__core.hpp>
#include <kernel/kerror.hpp>
#include <kernel/kobject/KDriverObject.hpp>
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/kobject/KObjectT.hpp>

namespace kernel {
struct KVolumeMountObject;
struct KVFSNode;
struct KVFSDirectoryEntry;
enum class VFSFileOperation : uint32_t;

using KVolumeIdProbeResult = kstd::expected<bool, kstd::error_code>;
using KVolumeIdProbe = cfunc<KVolumeIdProbeResult(const KDevicePtr<KDevice> &, uint32_t &)>;

struct KFilesystemObject : KObjectT<KFilesystemObject, 1, TYPE_FILE_SYSTEM> {
  using KObjectT::KObjectT;
  KObjectPtr<KDriverObject> driver;

  cfunc<kstd::error_code(const KObjectPtr<KVolumeMountObject> &partial, const kstd::string_view &options)> MountVolume;// < Populates the volume mount object with opaque options
  cfunc<kstd::error_code(const KObjectPtr<KVolumeMountObject> &)> UnmountVolume;
  cfunc<kstd::error_code(const KObjectPtr<KVolumeMountObject> &)> VerifyVolume;

  cfunc<kstd::error_code(const KObjectPtr<KVolumeMountObject> &, KVFSNode &rootOut)> GetRootNode;

  // Lookup a child inside a directory vnode. Returns NotFound when the name does not exist.
  cfunc<kstd::error_code(const KObjectPtr<KVolumeMountObject> &,
                    const KVFSNode &parent,
                    kstd::string_view name,
                    KVFSNode &out)>
      Lookup;

  cfunc<kstd::expected<bool, kstd::error_code>(const KObjectPtr<KVolumeMountObject> &,
      const KVFSNode &, uint32_t index, KVFSDirectoryEntry &)> ReadDirectory;
  cfunc<kstd::error_code(const KObjectPtr<KVolumeMountObject> &, VFSFileOperation,
      const KVFSNode &parent, kstd::string_view name,
      const KVFSNode *destinationParent, kstd::string_view destinationName)> OperateFile;

  // Create a handle for a vnode
  cfunc<kstd::error_code(const KObjectPtr<KVolumeMountObject> &, const KVFSNode &, KIOObject::OpenMode)> CreateHandle;
  cfunc<void(const KObjectPtr<KIOObject> &)> CloseHandle;//< Can be null, in that case the kernel will use the default implementation
  // VFS calls this with the mount's fileMutex held. Read out.size bytes at offset into out.
  // Success with 0 bytes means end of file.
  cfunc<kstd::expected<uint64_t, kstd::error_code>(const KObjectPtr<KVolumeMountObject> &, const KVFSNode &, uint64_t offset, const kstd::span<uint8_t> &out)> ReadHandle;
  // Write data at offset, return the number of bytes written (may be short).
  cfunc<kstd::expected<uint64_t, kstd::error_code>(const KObjectPtr<KVolumeMountObject> &, const KVFSNode &, uint64_t offset, const kstd::span<const uint8_t> &data)> WriteHandle;

  // Returns true and writes the recognized volume ID. Returns false for a
  // non-candidate, or an error when the probe cannot complete.
  KVolumeIdProbe ProbeVolumeId;
};

template<>
struct ObjectTypeOf<TYPE_FILE_SYSTEM> {
  using type = KFilesystemObject;
};
}// namespace kernel
