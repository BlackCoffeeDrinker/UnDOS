
#include "stdkrn.hpp"
#include "syscalls.hpp"

uint32_t handle_Print_V1(__KRN_DEBUG_PRINT_V1 &&args) {
  early_print_fmt("{}", args.message);
  return 0;
}
