#pragma once

#include <kernel/__core.hpp>
#include <kernel/data_buffer.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/mutex.hpp>

namespace kernel {
struct KVFSNode;
struct KFilesystemObject;
struct KIOObject;

struct KVolumeMountObject : KObjectT<KVolumeMountObject, 1, TYPE_VOLUME_MOUNT> {
  using KObjectT::KObjectT;
  KDevicePtr<KDevice> volume;              //< Partition
  KObjectPtr<KFilesystemObject> fileSystem;//< Filesystem driver used

  DataBuffer driverExtension;

  uint32_t serialNumber;
  kstd::static_string<32> volumeLabel;

  bool isMounted;
  bool isLocked;
  bool isExclusive;
  Mutex fileMutex;
  uint32_t openFileCount{0};
  KIOObject *openFiles{nullptr};
};

template<>
struct ObjectTypeOf<TYPE_VOLUME_MOUNT> {
  using type = KVolumeMountObject;
};
}// namespace kernel
