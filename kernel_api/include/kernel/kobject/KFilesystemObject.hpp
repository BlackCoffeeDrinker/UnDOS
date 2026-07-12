#pragma once

#include <kernel/__core.hpp>
#include <kernel/kobject/KDriverObject.hpp>
#include <kernel/kobject/KObjectT.hpp>

namespace kernel {

struct KVolumeMountObject;
struct KFileObject;

struct KFilesystemObject : KObjectT<KFilesystemObject, 1, TYPE_FILE_SYSTEM> {
  KObjectPtr<KDriverObject> driver;

  cfunc<bool(const KObjectPtr<KVolumeMountObject> &, const kstd::string_view &)> MountVolume;
  cfunc<bool(const KObjectPtr<KVolumeMountObject> &)> UnmountVolume;

  cfunc<bool(const KObjectPtr<KFileObject> &)> CreateHandle;
  cfunc<void(const KObjectPtr<KFileObject> &)> CloseHandle; // Can be null, in that case the kernel will use the default implementation
  cfunc<uint64_t(const KObjectPtr<KFileObject> &, uint64_t)> SeekHandle;// Can be null, in that case the kernel will use the default implementation
  cfunc<uint64_t(const KObjectPtr<KFileObject> &, const kstd::span<uint8_t> &)> ReadHandle;
  cfunc<uint64_t(const KObjectPtr<KFileObject> &, const kstd::span<const uint8_t> &)> WriteHandle;

  // QueryDirectory
  // QueryMetadata
};

template<>
struct ObjectTypeOf<TYPE_FILE_SYSTEM> {
  using type = KFilesystemObject;
};
}// namespace kernel
