#include "stdkrn.hpp"
#include "syscalls.hpp"

uint32_t handle_CreateProcess_V1(__KRN_PROC_CREATEPROCESS_V1 &&args) {
  (void) args;
  return 0;
}
/** Retrieves information about a running or exited process. */
uint32_t handle_GetInfo_V1(__KRN_PROC_GETINFO_V1 &&args) {
  (void) args;
  return 0;
}
/** Terminates the current process with the given return code. */
uint32_t handle_Exit_V1(__KRN_PROC_EXIT_V1 &&args) {
  (void) args;
  KE_SCHED_Terminate();
}
/** Allocates `size` bytes of pages with the given protection. */
uint32_t handle_AllocatePages_V1(__KRN_MEM_ALLOCATEPAGES_V1 &&args) {
  (void) args;
  return 0;
}
/** Frees a previously allocated page range. */
uint32_t handle_FreePages_V1(__KRN_MEM_FREEPAGES_V1 &&args) {
  (void) args;
  return 0;
}
/** Changes the protection of an existing page range (e.g. mark executable). */
uint32_t handle_Protect_V1(__KRN_MEM_PROTECT_V1 &&args) {
  (void) args;
  return 0;
}
