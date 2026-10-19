#include "backend.h"
#include <errno.h>
#include <userspace.h>

static int status(uint32_t code) {
  switch (code) {
    case SyscallReturnCode_OKAY: return 0;
    case SyscallReturnCode_OUT_OF_MEMORY: return ENOMEM;
    case SyscallReturnCode_UNSUPPORTED: return ENOSYS;
    default: return EIO;
  }
}
_Noreturn void undos_libc_terminate(int code) {
  (void) UD_SYS_ExitV1((uint32_t)code);
  __builtin_trap();
}
void undos_libc_startup_error(void) {
  (void) UD_SYS_PrintV1("CRT: incompatible application startup ABI\n");
}
int undos_libc_read(HANDLE handle, void *buffer, size_t *size) {
  uint32_t length = (uint32_t)*size;
  const int error = status(UD_SYS_ReadV1(handle, buffer, &length));
  *size = error ? 0 : length;
  return error;
}
int undos_libc_write(HANDLE handle, const void *buffer, size_t *size) {
  uint32_t length = 0;
  const int error = status(UD_SYS_WriteV2(handle, buffer, (uint32_t)*size, &length));
  *size = error ? 0 : length;
  return error;
}
int undos_libc_open(const char *path, HANDLE *handle) {
  return status(UD_SYS_OpenV1(path, FileOpenMode_Read, handle));
}
int undos_libc_close(HANDLE handle) { return status(UD_SYS_CloseV1(handle)); }
void *undos_libc_allocate_pages(size_t size) {
  uint32_t address = 0;
  if (UD_SYS_AllocatePagesV1((uint32_t)size,
      MemoryProtectFlags_Read | MemoryProtectFlags_Write,
      &address) != SyscallReturnCode_OKAY) return NULL;
  return (void *)(uintptr_t)address;
}
