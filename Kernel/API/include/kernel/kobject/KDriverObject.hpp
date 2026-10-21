#pragma once

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/kerror.hpp>
#include <kernel/fwd/KConfigurationKey.hpp>
#include <kernel/fwd/KDevicePtr.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

#include <expected.hpp>
#include <span.hpp>

namespace kernel {
struct KDeviceObject;
struct DmaOperation;
struct DmaDescription;
struct IoResourceRequirementsList;

struct KDriverObject : KObjectT<KDriverObject, 1, TYPE_DRIVER> {
  using KObjectT::KObjectT;
  VirtualAddress load_base{0};
  size_t total_size{0};
  cfunc<void(KObjectPtr<KDriverObject> &, const KConfigurationKeyPtr &)> entry_point;

  // NOTE: The generic KEvent EventHandler was removed for now; drivers are
  // driven through the explicit PnP method pointers below (raw method calls).
  // Events may return if a richer event/IRP-style model is needed later.

  // region PnP
  cfunc<size_t(const KDevicePtr<KDevice> &)> GetChildCount;
  cfunc<void(const KDevicePtr<KDevice> &)> EnumerateDevices;
  // Success once the driver has attached its device; any other status abandons the device.
  cfunc<kstd::error_code(const KObjectPtr<KDriverObject> &, const KDevicePtr<KDevice> &)> AddDevice;
  cfunc<void(const KDevicePtr<KDevice> &)> StartDevice;
  cfunc<KDevicePtr<KDevice>(const KDevicePtr<KDevice> &, size_t)> GetChild;
  cfunc<kstd::string_view(const KDevicePtr<KDevice> &)> GetHardwareId;
  // NotFound/Unsupported lets KE_DMA_GetAdapter continue down the stack and to the parent bus.
  cfunc<kstd::expected<DmaOperation *, kstd::error_code>(const KDevicePtr<KDevice> &, const DmaDescription &)> GetDmaAdapter;
  // Queried by the PnP manager against a child PDO to learn the fixed resources
  // its enumerator assigned to it. A successful nullptr means "no resources"; failure is reported as an error.
  cfunc<kstd::expected<IoResourceRequirementsList *, kstd::error_code>(const KDevicePtr<KDevice> &)> QueryResourceRequirements;
  // endregion

  // region Block I/O
  // Synchronous byte-offset block I/O dispatch. The upper device (e.g. a
  // Volume) adds its base offset and range-checks before forwarding down the
  // stack; the lowest driver (e.g. ATA) translates the byte offset into LBA
  // sector transfers. The buffer span carries both the storage and its length;
  // `transferred` receives the number of bytes moved.
  cfunc<kstd::error_code(const KDevicePtr<KDevice> &, uint64_t, kstd::span<uint8_t>, size_t &)> Read;
  cfunc<kstd::error_code(const KDevicePtr<KDevice> &, uint64_t, kstd::span<const uint8_t>, size_t &)> Write;
  // endregion

  // region Device control
  // Synchronous control request (code, input payload, output buffer). The
  // driver sets `transferred` to the bytes written to the output buffer and
  // returns KernelError::Unsupported for codes it does not handle.
  cfunc<kstd::error_code(const KDevicePtr<KDevice> &, uint32_t, kstd::span<const uint8_t>, kstd::span<uint8_t>, size_t &)> DeviceControl;
  // endregion
};

template<>
struct ObjectTypeOf<TYPE_DRIVER> {
  using type = KDriverObject;
};
}// namespace kernel
