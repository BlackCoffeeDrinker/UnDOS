#pragma once

#include <stddef.h>
#include <stdint.h>
#include <undos/syscalls/__aliases.h>

#ifdef __cplusplus
extern "C" {
#endif
// The syscall dispatcher limits each mapped buffer to 4096 bytes.
#define UNDOS_LIBC_IO_CHUNK_SIZE 4096
int undos_libc_read(HANDLE handle, void *buffer, size_t *size);
int undos_libc_write(HANDLE handle, const void *buffer, size_t *size);
int undos_libc_open(const char *path, HANDLE *handle);
int undos_libc_close(HANDLE handle);
void *undos_libc_allocate_pages(size_t size);
#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void undos_libc_terminate(int code);
void undos_libc_startup_error(void);
#ifdef __cplusplus
}
#endif
