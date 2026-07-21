#include "stdkrn.hpp"
#include "syscalls.hpp"

uint32_t handle_CreateProcess_V1(__KRN_PROC_CREATEPROCESS_V1 &&args, uint32_t flags) {
  (void) args;
  (void) flags;
  return 0;
}
/** Retrieves information about a running or exited process. */
uint32_t handle_GetInfo_V1(__KRN_PROC_GETINFO_V1 &&args, uint32_t flags) {
  (void) args;
  (void) flags;
  return 0;
}
/** Terminates the current process with the given return code. */
uint32_t handle_Exit_V1(__KRN_PROC_EXIT_V1 &&args, uint32_t flags) {
  (void) args;
  (void) flags;
  KE_SCHED_Terminate();
}
/** Allocates `size` bytes of pages with the given protection. */
uint32_t handle_AllocatePages_V1(__KRN_MEM_ALLOCATEPAGES_V1 &&args, uint32_t flags) {
  (void) args;
  (void) flags;
  return 0;
}
/** Frees a previously allocated page range. */
uint32_t handle_FreePages_V1(__KRN_MEM_FREEPAGES_V1 &&args, uint32_t flags) {
  (void) args;
  (void) flags;
  return 0;
}
/** Changes the protection of an existing page range (e.g. mark executable). */
uint32_t handle_Protect_V1(__KRN_MEM_PROTECT_V1 &&args, uint32_t flags) {
  (void) args;
  (void) flags;
  return 0;
}
/** Reads data from an open file descriptor into `data`. */
uint32_t handle_Read_V1(__KRN_FS_READ_V1 &&args, uint32_t flags) {
  (void) args;
  (void) flags;
  return 0;
}
/** Writes data from `data` to an open file descriptor. */
uint32_t handle_Write_V1(__KRN_FS_WRITE_V1 &&args, uint32_t flags) {
  (void) args;
  (void) flags;
  return 0;
}
