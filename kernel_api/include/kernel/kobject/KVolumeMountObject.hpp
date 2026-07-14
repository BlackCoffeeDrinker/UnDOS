#pragma once

#include <kernel/__core.hpp>
#include <kernel/data_buffer.hpp>
#include <kernel/kobject/KObjectT.hpp>

namespace kernel {
struct KVFSNode;
struct KFilesystemObject;

struct KVolumeMountObject : KObjectT<KVolumeMountObject, 1, TYPE_VOLUME_MOUNT> {
  KDevicePtr<KDevice> volume;              //< Partition
  KObjectPtr<KFilesystemObject> fileSystem;//< Filesystem driver used

  DataBuffer driverExtension;

  uint32_t serialNumber;
  kstd::static_string<32> volumeLabel;

  bool isMounted;
  bool isLocked;
  bool isExclusive;
};

template<>
struct ObjectTypeOf<TYPE_VOLUME_MOUNT> {
  using type = KVolumeMountObject;
};
}// namespace kernel
