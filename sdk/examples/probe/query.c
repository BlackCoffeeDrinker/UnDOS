#include <userspace.h>

int sdk_query_from_c(void) {
  enum FpuCapability capability;
  return UD_SYS_QueryFpuV1(&capability) == SyscallReturnCode_OKAY;
}
