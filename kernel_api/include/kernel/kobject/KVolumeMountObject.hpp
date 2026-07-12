#pragma once

#include <kernel/__core.hpp>
#include <kernel/data_buffer.hpp>
#include <kernel/kobject/KFilesystemObject.hpp>
#include <kernel/kobject/KObjectT.hpp>

namespace kernel {
struct KVolumeMountObject : KObjectT<KVolumeMountObject, 1, TYPE_VOLUME_MOUNT> {
  KDevicePtr<KDevice> volume;              //< Partition
  KObjectPtr<KFilesystemObject> filesystem;//< Filesystem driver used

  DataBuffer driverExtension;

  uint32_t serialNumber;
  kstd::static_string<32> volumeLabel;

  bool isMounted;
  bool isLocked;
  bool isExclusive;
  
  // Populated by the driver at mount
  cfunc<bool(const KObjectPtr<KVolumeMountObject> &)> verifyVolume;
};

template<>
struct ObjectTypeOf<TYPE_VOLUME_MOUNT> {
  using type = KVolumeMountObject;
};
}// namespace kernel
