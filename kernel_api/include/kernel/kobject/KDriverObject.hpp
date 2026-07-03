#pragma once

#include <kernel/__core.hpp>
#include <kernel/kobject/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

struct KEvent;
struct KPhysicalDeviceObject;
struct DmaOperation;

struct KDriverObject : KObjectT<KDriverObject, 1, TYPE_DRIVER> {
  VirtualAddress load_base{0};
  size_t total_size{0};
  cfunc<void(KObjectPtr<KDriverObject> &)> entry_point;

  cfunc<void(const KObjectPtr<KDriverObject>&, const KEvent &)> eventHandler;

  // region PnP
  cfunc<void(const KObjectPtr<KPhysicalDeviceObject>&)> StartDevice;
  cfunc<void(const KObjectPtr<KPhysicalDeviceObject>&)> EnumerateDevices;
  cfunc<size_t(const KObjectPtr<KPhysicalDeviceObject>&)> GetChildCount;
  cfunc<KObjectPtr<KPhysicalDeviceObject>(const KObjectPtr<KPhysicalDeviceObject>&, size_t)> GetChild;
  cfunc<kstd::string_view(const KObjectPtr<KPhysicalDeviceObject>&)> GetHardwareId;
  cfunc<DmaOperation*(const KObjectPtr<KPhysicalDeviceObject>&)> GetDmaAdapter;
  cfunc<bool(const KObjectPtr<KDriverObject>&, const KObjectPtr<KPhysicalDeviceObject>&)> AddDevice;
  // endregion
};

} // namespace kernel
