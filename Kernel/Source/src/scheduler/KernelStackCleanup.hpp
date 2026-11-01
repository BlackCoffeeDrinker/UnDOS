#pragma once

#include <kernel/memory/address.hpp>

namespace kernel::sched::detail {

template<typename Insert, typename Cleanup>
auto insert_or_cleanup(Insert &&insert, Cleanup &&cleanup) {
  auto status = insert();
  if (status) cleanup();
  return status;
}

template<typename GetPhysical, typename Unmap, typename FreeFrames, typename FreeRegion>
auto cleanup_kernel_stack(VirtualAddress base, size_t pageCount, size_t pageSize,
                          GetPhysical &&getPhysical, Unmap &&unmap,
                          FreeFrames &&freeFrames, FreeRegion &&freeRegion) {
  for (size_t i = 0; i < pageCount; ++i) {
    const auto page = base + i * pageSize;
    const auto phys = getPhysical(page);
    unmap(page);
    if (phys) freeFrames(phys, 1);
  }
  return freeRegion(base.as_ptr());
}

}// namespace kernel::sched::detail
