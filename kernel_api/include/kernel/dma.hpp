
#pragma once
#include "io.hpp"

namespace kernel {

struct DmaOperation : Versioned<DmaOperation, 1> {
  cfunc<void*(DmaOperation* self, size_t size, PhysicalAddress* physicalAddress, bool cacheEnabled)> AllocateCommonBuffer;
  cfunc<void(DmaOperation* self, size_t size, PhysicalAddress physicalAddress, void* virtualAddress)> FreeCommonBuffer;
};

} // namespace kernel

// Returns null on failure
UNDOS_KERNEL_API kernel::DmaOperation* KE_DMA_GetAdapter(const kernel::KObjectPtr<kernel::KPhysicalDeviceObject> & device);
