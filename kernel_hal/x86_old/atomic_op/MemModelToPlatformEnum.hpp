
#pragma once

#include <Kernel/hal_interface.hpp>

constexpr PlatformMemoryOrder to_memory_order(int model) noexcept {
  switch (model) {
    case 0:
      return PlatformMemoryOrder::Relaxed;
    case 1:
      return PlatformMemoryOrder::Consume;
    case 2:
      return PlatformMemoryOrder::Acquire;
    case 3:
      return PlatformMemoryOrder::Release;
    case 4:
      return PlatformMemoryOrder::AcquireRelease;
    case 5:
    default:
      return PlatformMemoryOrder::SequentiallyConsistent;
  }
}
