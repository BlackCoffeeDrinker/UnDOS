#pragma once

#include <memory.hpp>

#include <kernel/__core.hpp>
#include <kernel/kobject/KDriverObject.hpp>
#include <kernel/kobject/KFileObject.hpp>
#include <kernel/kobject/KObjectT.hpp>

namespace kernel {
struct KVolumeMountObject;
struct KVFSNode;

struct KFilesystemObject : KObjectT<KFilesystemObject, 1, TYPE_FILE_SYSTEM> {
  KObjectPtr<KDriverObject> driver;

  cfunc<bool(const KObjectPtr<KVolumeMountObject> &partial, const kstd::string_view &options)> MountVolume;// < Populates the volume mount object with opaque options
  cfunc<bool(const KObjectPtr<KVolumeMountObject> &)> UnmountVolume;
  cfunc<bool(const KObjectPtr<KVolumeMountObject> &)> VerifyVolume;

  cfunc<bool(const KObjectPtr<KVolumeMountObject> &, KVFSNode &rootOut)> GetRootNode;

  // Lookup a child inside a directory vnode
  cfunc<bool(const KObjectPtr<KVolumeMountObject> &,
             const KVFSNode &parent,
             kstd::string_view name,
             KVFSNode &out)>
      Lookup;

  // Create a handle for a vnode
  cfunc<bool(const KObjectPtr<KVolumeMountObject> &, const KVFSNode &, KFileObject::OpenMode)> CreateHandle;                                    //
  cfunc<void(const KObjectPtr<KFileObject> &)> CloseHandle;                                                                                     //< Can be null, in that case the kernel will use the default implementation
  cfunc<uint64_t(const KObjectPtr<KVolumeMountObject> &, const KVFSNode &, uint64_t offset, const kstd::span<uint8_t> &out)> ReadHandle;        //< Read out.size bytes at offset into out, return the number of bytes read
  cfunc<uint64_t(const KObjectPtr<KVolumeMountObject> &, const KVFSNode &, uint64_t offset, const kstd::span<const uint8_t> &data)> WriteHandle;//< Write data at offset, return the number of bytes written
};

template<>
struct ObjectTypeOf<TYPE_FILE_SYSTEM> {
  using type = KFilesystemObject;
};
}// namespace kernel
