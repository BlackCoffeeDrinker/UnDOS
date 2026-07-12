
#pragma once

#include <kernel/__core.hpp>
#include <kernel/common/DoubleList.hpp>
#include <kernel/device.hpp>
#include <kernel/fwd/KDevicePtr.hpp>

namespace kernel {
// The kind of hardware resource a descriptor refers to. Mirrors the classic
// NT CM_RESOURCE_TYPE set (Port / Memory / Interrupt / Dma).
enum class ResourceType : uint8_t {
  Null = 0,
  Port,     // I/O port range
  Memory,   // memory-mapped range
  Interrupt,// IRQ line / vector
  Dma,      // DMA channel
};

// Sharing disposition of a resource. Two descriptors of the same type that
// overlap only conflict when at least one is not Shared.
enum class ResourceShare : uint8_t {
  DeviceExclusive = 0,
  Shared,
};

// Whether a descriptor is a hard requirement or one of several acceptable
// choices. Only Fixed is honoured by the current reservation-only manager;
// Preferred/Alternative are reserved for a future arbiter.
enum class ResourceOption : uint8_t {
  Preferred = 0,
  Alternative,
  Fixed,
};

// A single resource requirement (IO_RESOURCE_DESCRIPTOR). The range is kept as
// a flat [start,end] window so the fixed-reservation math is trivial while a
// future arbiter can still treat it as a [min,max] allocation window.
struct IoResourceDescriptor : common::Link<IoResourceDescriptor> {
  ResourceType type = ResourceType::Null;
  ResourceShare share = ResourceShare::DeviceExclusive;
  ResourceOption option = ResourceOption::Fixed;
  uint32_t start = 0; // io/irq/dma base for Fixed; range min otherwise
  uint32_t end = 0;   // range max (== start for Fixed)
  uint32_t length = 0;// ports/bytes (1 for irq/dma)
  uint32_t alignment = 1;
};

// One acceptable configuration (IO_RESOURCE_LIST): a set of descriptors that
// must all be satisfied together.
struct IoResourceList : common::Link<IoResourceList> {
  common::DoubleList<IoResourceDescriptor> descriptors;
};

// All alternative configurations for a device, in priority order
// (IO_RESOURCE_REQUIREMENTS_LIST).
struct IoResourceRequirementsList {
  DeviceType interfaceType{device_type::Unknown};
  common::DoubleList<IoResourceList> alternatives;
};

// A single granted resource (CM_PARTIAL_RESOURCE_DESCRIPTOR).
struct CmPartialResourceDescriptor : common::Link<CmPartialResourceDescriptor> {
  ResourceType type = ResourceType::Null;
  ResourceShare share = ResourceShare::DeviceExclusive;
  uint16_t flags = 0;
  uint32_t start = 0; // granted io/irq/dma value
  uint32_t length = 0;// span (1 for irq/dma)
};

// The full grant handed back to a device (CM_RESOURCE_LIST).
struct CmResourceList {
  common::DoubleList<CmPartialResourceDescriptor> descriptors;
};

}// namespace kernel

// region Builders
/**
 * @ingroup RES
 * @brief Method KE_RES_CreateRequirementsList
 * 
 * Allocate an empty requirements list. Returns null on allocation failure.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::IoResourceRequirementsList*,
    KE_RES_CreateRequirementsList,
    kernel::DeviceType interfaceType);

/**
 * @ingroup RES
 * @brief Method KE_RES_AddAlternative
 * 
 * Append a new (empty) alternative configuration to a requirements list.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::IoResourceList*,
    KE_RES_AddAlternative,
    kernel::IoResourceRequirementsList *list);

/**
 * @ingroup RES
 * @brief Method KE_RES_AddPort
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_RES_AddPort,
    kernel::IoResourceList *alt, uint32_t start, uint32_t length, kernel::ResourceOption option, kernel::ResourceShare share);

/**
 * @ingroup RES
 * @brief Method KE_RES_AddInterrupt
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_RES_AddInterrupt,
    kernel::IoResourceList *alt, uint32_t vector, kernel::ResourceOption option, kernel::ResourceShare share);

/**
 * @ingroup RES
 * @brief Method KE_RES_AddMemory
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_RES_AddMemory,
    kernel::IoResourceList *alt, uint32_t start, uint32_t length, kernel::ResourceOption option, kernel::ResourceShare share);

/**
 * @ingroup RES
 * @brief Method KE_RES_AddDma
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_RES_AddDma,
    kernel::IoResourceList *alt, uint32_t channel, kernel::ResourceOption option, kernel::ResourceShare share);

/**
 * @ingroup RES
 * @brief Method KE_RES_FreeRequirementsList
 *
 * Free a requirements list and every node it owns. Safe to call with null.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_RES_FreeRequirementsList,
    kernel::IoResourceRequirementsList *list);
// endregion

// region Assignment / lookup
/**
 * @ingroup RES
 * @brief Method KE_RES_AssignResources
 * 
 * Reserve the device's resources. A null requirements list succeeds with an
 * empty grant. Returns false (reserving nothing) if any fixed range conflicts
 * with an already-reserved range.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    bool,
    KE_RES_AssignResources,
    const kernel::KDevicePtr<kernel::KDevice> &device, kernel::IoResourceRequirementsList *requirements);

/**
 * @ingroup RES
 * @brief Method KE_RES_GetAssignedResources
 * 
 */
UNDOS_KERNEL_PUBLIC_V1API(
    const kernel::CmResourceList *,
    KE_RES_GetAssignedResources,
    const kernel::KDevicePtr<kernel::KDevice> &device);

/**
 * @ingroup RES
 * @brief Method KE_RES_FindAssigned
 * 
 * Return the index-th granted descriptor of the given type, or null.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    const kernel::CmPartialResourceDescriptor *,
    KE_RES_FindAssigned,
    const kernel::KDevicePtr<kernel::KDevice> &device, kernel::ResourceType type, size_t index);

/**
 * @ingroup RES
 * @brief Method KE_RES_ReleaseResources
 * 
 * Release a device's grant, freeing its reserved ranges.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_RES_ReleaseResources,
    const kernel::KDevicePtr<kernel::KDevice> &device);
// endregion
