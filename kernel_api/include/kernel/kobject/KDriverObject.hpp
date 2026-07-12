#pragma once

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/fwd/KDevicePtr.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

#include <span.hpp>

namespace kernel {
struct KDeviceObject;
struct DmaOperation;
struct DmaDescription;
struct IoResourceRequirementsList;

// Result of a synchronous block I/O dispatch (KE_IO_ReadDevice/WriteDevice).
enum class IoStatus : uint8_t {
  Success,
  InvalidParameter,
  OutOfRange,
  Unsupported,
  DeviceError,
};

struct KDriverObject : KObjectT<KDriverObject, 1, TYPE_DRIVER> {
  VirtualAddress load_base{0};
  size_t total_size{0};
  cfunc<void(KObjectPtr<KDriverObject> &)> entry_point;

  // NOTE: The generic KEvent EventHandler was removed for now; drivers are
  // driven through the explicit PnP method pointers below (raw method calls).
  // Events may return if a richer event/IRP-style model is needed later.

  // region PnP
  cfunc<size_t(const KDevicePtr<KDevice> &)> GetChildCount;
  cfunc<void(const KDevicePtr<KDevice> &)> EnumerateDevices;
  cfunc<bool(const KObjectPtr<KDriverObject> &, const KDevicePtr<KDevice> &)> AddDevice;
  cfunc<void(const KDevicePtr<KDevice> &)> StartDevice;
  cfunc<KDevicePtr<KDevice>(const KDevicePtr<KDevice> &, size_t)> GetChild;
  cfunc<kstd::string_view(const KDevicePtr<KDevice> &)> GetHardwareId;
  cfunc<DmaOperation *(const KDevicePtr<KDevice> &, const DmaDescription &)> GetDmaAdapter;
  // Queried by the PnP manager against a child PDO to learn the fixed resources
  // its enumerator assigned to it. Returning nullptr means "no resources".
  cfunc<IoResourceRequirementsList *(const KDevicePtr<KDevice> &)> QueryResourceRequirements;
  // endregion

  // region Block I/O
  // Synchronous byte-offset block I/O dispatch. The upper device (e.g. a
  // Volume) adds its base offset and range-checks before forwarding down the
  // stack; the lowest driver (e.g. ATA) translates the byte offset into LBA
  // sector transfers. The buffer span carries both the storage and its length;
  // `transferred` receives the number of bytes moved.
  cfunc<IoStatus(const KDevicePtr<KDevice> &, uint64_t, kstd::span<uint8_t>, size_t &)> Read;
  cfunc<IoStatus(const KDevicePtr<KDevice> &, uint64_t, kstd::span<const uint8_t>, size_t &)> Write;
  // endregion
};

template<>
struct ObjectTypeOf<TYPE_DRIVER> {
  using type = KDriverObject;
};
}// namespace kernel
