#pragma once

// Minimal freestanding stdio for userland. Supports a small, pragmatic
// subset of printf: %s, %c, %d, %u, %x, %p and %%.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <undos/syscalls/__aliases.h>
#include <undos/syscalls/__enum_StdHandleSelector.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns bytes submitted on success, or -1 for an invalid argument,
// unavailable stdout, syscall failure, or formatting overflow. WriteV1 does
// not expose partial-write counts; success reports the submitted byte count.
int crt_puts(const char *message);
int crt_printf(const char *fmt, ...);
int crt_vprintf(const char *fmt, va_list args);

// Fetches and caches the process's stdin, stdout, and stderr handles.
// Returns 0 only when all three streams are available; otherwise returns -1.
int crt_initialize_stdio(void);
// Copies a cached opaque handle to *handle; returns -1 before initialization,
// for an unavailable stream, or for an invalid argument.
int crt_get_std_handle(enum StdHandleSelector selector, HANDLE *handle);

int crt_getchar(void);
char *crt_readline(char *buffer, size_t size);

#ifdef __cplusplus
}
#endif
