#include "stdkrn.hpp"
#include "syscalls.hpp"

SyscallReturnCode handle_QueryFpu_V1(__KRN_ABI_QUERYFPU_V1 &&args) {
  // HAL_CPU_SwitchContext currently preserves only callee-saved integer
  // registers, and no kernel x87 emulator is installed.
  args.capability = FpuCapability_Unavailable;
  return SyscallReturnCode_OKAY;
}
