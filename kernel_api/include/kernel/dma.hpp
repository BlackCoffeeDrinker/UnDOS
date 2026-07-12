
#pragma once

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/common/DoubleList.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/io.hpp>

namespace kernel {
struct KDeviceObject;

enum class DmaDirection {
  Read, // From device to memory
  Write,// From memory to device
  SystemToDevice = Write,
  DeviceToSystem = Read,
};

enum class DmaWidth {
  Width8Bit,
  Width16Bit,
  Width32Bit,
  Width64Bit,
};

enum class DmaSpeed {
  Compatible,
  TypeA,
  TypeB,
  TypeC,
};

enum class DmaMode {
  Single,
  Block,
  Demand,
  Cascade,
};

enum class DmaAllocationFlags {
  None = 0,
  CacheEnabled = 1 << 0,
};

enum class DmaAction {
  KeepObject,
  DeallocateObject,
  DeallocateObjectKeepRegisters,
};

struct DmaDescription {
  DmaWidth width;
  DmaMode mode;
  bool isMaster;
  bool autoInitialize;
  uint32_t channel;// For bus types that use channels (ISA/EISA/MCA)
  uint32_t port;   // For bus types that use ports
  uint32_t maximumLength;
};

struct ScatterGatherElement : common::Link<ScatterGatherElement> {
  PhysicalAddress address;
  uint32_t length;
  uintptr_t reserved;
};

struct ScatterGatherList {
  uintptr_t reserved;
  common::DoubleList<ScatterGatherElement> elements;
};

struct DmaOperation : Versioned<DmaOperation, 1> {
  cfunc<VirtualAddress(DmaOperation *self, size_t size, PhysicalAddress *physicalAddress, DmaAllocationFlags flags)> AllocateCommonBuffer;
  cfunc<void(DmaOperation *self, size_t size, PhysicalAddress physicalAddress, VirtualAddress virtualAddress)> FreeCommonBuffer;
  cfunc<bool(DmaOperation *self, const KDevicePtr<KDevice> &deviceObject, uint32_t numberOfMapRegisters, cfunc<DmaAction(const KDevicePtr<KDevice> &deviceObject, VirtualAddress context, VirtualAddress mapRegisterBase, VirtualAddress reserved)> executionRoutine, VirtualAddress context)> AllocateAdapterChannel;
  cfunc<void(DmaOperation *self, VirtualAddress mapRegisterBase)> FreeAdapterChannel;
  cfunc<void(DmaOperation *self, VirtualAddress mapRegisterBase, uint32_t numberOfMapRegisters)> FreeMapRegisters;
  cfunc<PhysicalAddress(DmaOperation *self, VirtualAddress mapRegisterBase, VirtualAddress virtualAddress, size_t *length, DmaDirection direction)> MapTransfer;
  cfunc<bool(DmaOperation *self, const KDevicePtr<KDevice> &deviceObject, VirtualAddress virtualAddress, size_t length, DmaDirection direction, cfunc<void(const KDevicePtr<KDevice> &deviceObject, VirtualAddress context, ScatterGatherList *sgList)> executionRoutine, VirtualAddress context)> GetScatterGatherList;
  cfunc<void(DmaOperation *self, ScatterGatherList *sgList, DmaDirection direction)> PutScatterGatherList;
  cfunc<size_t(DmaOperation *self)> GetDmaAlignment;
  cfunc<void(DmaOperation *self, PhysicalAddress address, size_t count, DmaDirection direction)> SetupTransfer;
  cfunc<void(DmaOperation *self)> StopTransfer;
  cfunc<size_t(DmaOperation *self)> ReadCounter;

  // For systems with non-coherent caches
  cfunc<void(DmaOperation *self, VirtualAddress virtualAddress, size_t size, DmaDirection direction)> FlushAdapterBuffers;
};

}// namespace kernel

// Returns null on failure
/**
 * @ingroup DMA
 * @brief Method KE_DMA_GetAdapter
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::DmaOperation *,
    KE_DMA_GetAdapter,
    const kernel::KDevicePtr<kernel::KDevice> &device, const kernel::DmaDescription &description);

/**
 * @ingroup DMA
 * @brief Method KE_DMA_AllocateAdapterChannel
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_DMA_AllocateAdapterChannel,
    kernel::DmaOperation *self, const kernel::KDevicePtr<kernel::KDevice> &deviceObject, uint32_t numberOfMapRegisters, kernel::cfunc<kernel::DmaAction(const kernel::KDevicePtr<kernel::KDevice> &deviceObject, kernel::VirtualAddress context, kernel::VirtualAddress mapRegisterBase, kernel::VirtualAddress reserved)> executionRoutine, kernel::VirtualAddress context);

/**
 * @ingroup DMA
 * @brief Method KE_DMA_FreeAdapterChannel
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_DMA_FreeAdapterChannel,
    kernel::DmaOperation *self, kernel::VirtualAddress mapRegisterBase);

/**
 * @ingroup DMA
 * @brief Method KE_DMA_FreeMapRegisters
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_DMA_FreeMapRegisters,
    kernel::DmaOperation *self, kernel::VirtualAddress mapRegisterBase, uint32_t numberOfMapRegisters);

/**
 * @ingroup DMA
 * @brief Method KE_DMA_MapTransfer
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::PhysicalAddress,
    KE_DMA_MapTransfer,
    kernel::DmaOperation *self, kernel::VirtualAddress mapRegisterBase, kernel::VirtualAddress virtualAddress, size_t *length, kernel::DmaDirection direction);

/**
 * @ingroup DMA
 * @brief Method KE_DMA_GetScatterGatherList
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_DMA_GetScatterGatherList,
    kernel::DmaOperation *self, const kernel::KDevicePtr<kernel::KDevice> &deviceObject, kernel::VirtualAddress virtualAddress, size_t length, kernel::DmaDirection direction, kernel::cfunc<void(const kernel::KDevicePtr<kernel::KDevice> &deviceObject, kernel::VirtualAddress context, kernel::ScatterGatherList *sgList)> executionRoutine, kernel::VirtualAddress context);

/**
 * @ingroup DMA
 * @brief Method KE_DMA_PutScatterGatherList
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_DMA_PutScatterGatherList,
    kernel::DmaOperation *self, kernel::ScatterGatherList *sgList, kernel::DmaDirection direction);
