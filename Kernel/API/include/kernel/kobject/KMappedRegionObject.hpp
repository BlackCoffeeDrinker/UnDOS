#pragma once

#include <kernel/__core.hpp>
#include <kernel/boot/boot_info.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

// A region of memory mapped by Stage 1.5 before the kernel started (kernel
// image, HAL image, boot stack, BootInfo, paging structures). Created from
// BootInfoT::mapped_memory so the information outlives the BootInfo pages.
struct KMappedRegionObject : KObjectT<KMappedRegionObject, 1, TYPE_MAPPED_REGION> {
  using KObjectT::KObjectT;
  MappedMemoryRegionType regionType = MappedMemoryRegionType::None;
  PhysicalAddress physicalBase;
  VirtualAddress virtualBase;
  size_t length = 0;
};

template<>
struct ObjectTypeOf<TYPE_MAPPED_REGION> {
  using type = KMappedRegionObject;
};

}// namespace kernel
