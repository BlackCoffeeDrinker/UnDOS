#pragma once

#include <undos/syscalls/__enum_SyscallReturnCode.h>
#include <undos/syscalls/__struct_VideoPresentPath.h>
#include <undos/syscalls/__struct_VideoPresentTopology.h>

namespace init {
template<typename EnumeratePaths>
bool confirm_existing_display_path(const VideoPresentTopology &topology,
                                   EnumeratePaths enumeratePaths) {
  if (topology.pathCount != 1) return false;
  VideoPresentPath path{};
  uint32_t capacity = 0, required = 0, written = 0;
  auto error = enumeratePaths(topology.generation, 0, 1, nullptr, &capacity, &required, &written);
  if (error != SyscallReturnCode_OKAY || required != 1 || written != 0) return false;
  capacity = 1;
  error = enumeratePaths(topology.generation, 0, 0, &path, &capacity, &required, &written);
  return error == SyscallReturnCode_OKAY && required == 1 && written == 1 && !topology.degraded;
}
}
