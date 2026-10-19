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

struct KFilesystemObject : KObjectT<KFilesystemObject, 1, TYPE_FILE_SYSTEM> {
  using KObjectT::KObjectT;
  KObjectPtr<KDriverObject> driver;

  cfunc<KernelError(const KObjectPtr<KVolumeMountObject> &partial, const kstd::string_view &options)> MountVolume;// < Populates the volume mount object with opaque options
  cfunc<KernelError(const KObjectPtr<KVolumeMountObject> &)> UnmountVolume;
  cfunc<KernelError(const KObjectPtr<KVolumeMountObject> &)> VerifyVolume;

  cfunc<KernelError(const KObjectPtr<KVolumeMountObject> &, KVFSNode &rootOut)> GetRootNode;

  // Lookup a child inside a directory vnode. Returns NotFound when the name does not exist.
  cfunc<KernelError(const KObjectPtr<KVolumeMountObject> &,
                    const KVFSNode &parent,
                    kstd::string_view name,
                    KVFSNode &out)>
      Lookup;

  // Create a handle for a vnode
  cfunc<KernelError(const KObjectPtr<KVolumeMountObject> &, const KVFSNode &, KIOObject::OpenMode)> CreateHandle;
  cfunc<void(const KObjectPtr<KIOObject> &)> CloseHandle;//< Can be null, in that case the kernel will use the default implementation
  // Read out.size bytes at offset into out. Success with 0 bytes means end of file.
  cfunc<kstd::expected<uint64_t, KernelError>(const KObjectPtr<KVolumeMountObject> &, const KVFSNode &, uint64_t offset, const kstd::span<uint8_t> &out)> ReadHandle;
  // Write data at offset, return the number of bytes written (may be short).
  cfunc<kstd::expected<uint64_t, KernelError>(const KObjectPtr<KVolumeMountObject> &, const KVFSNode &, uint64_t offset, const kstd::span<const uint8_t> &data)> WriteHandle;
};

template<>
struct ObjectTypeOf<TYPE_FILE_SYSTEM> {
  using type = KFilesystemObject;
};
}// namespace kernel
