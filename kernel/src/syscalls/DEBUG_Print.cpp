
#include "stdkrn.hpp"
#include "syscalls.hpp"

kstd::error_code handle_Print_V1(__KRN_DEBUG_PRINT_V1 &&args) {
  KE_PrintBuffer(args.message.data(), args.message.size());
  return {};
}
