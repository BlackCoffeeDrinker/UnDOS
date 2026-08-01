#include "stdkrn.hpp"
#include "syscalls.hpp"

SyscallReturnCode handle_CreateProcess_V1(__KRN_PROC_CREATEPROCESS_V1 &&args) {
  (void) args;
  return SyscallReturnCode_UNKNOWN;
}
/** Retrieves information about a running or exited process. */
SyscallReturnCode handle_GetInfo_V1(__KRN_PROC_GETINFO_V1 &&args) {
  (void) args;
  return SyscallReturnCode_UNKNOWN;
}
/** Terminates the current process with the given return code. */
SyscallReturnCode handle_Exit_V1(__KRN_PROC_EXIT_V1 &&args) {
  (void) args;
  KE_SCHED_Terminate();
}
/** Allocates `size` bytes of pages with the given protection. */
SyscallReturnCode handle_AllocatePages_V1(__KRN_MEM_ALLOCATEPAGES_V1 &&args) {
  (void) args;
  return SyscallReturnCode_UNKNOWN;
}
/** Frees a previously allocated page range. */
SyscallReturnCode handle_FreePages_V1(__KRN_MEM_FREEPAGES_V1 &&args) {
  (void) args;
  return SyscallReturnCode_UNKNOWN;
}
/** Changes the protection of an existing page range (e.g. mark executable). */
SyscallReturnCode handle_Protect_V1(__KRN_MEM_PROTECT_V1 &&args) {
  (void) args;
  return SyscallReturnCode_UNKNOWN;
}
