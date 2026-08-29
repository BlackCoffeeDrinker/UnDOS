#pragma once

// Minimal freestanding formatted output for userland, built on top of
// UD_SYS_PrintV1. Supports a small, pragmatic subset of printf: %s, %c,
// %d, %u, %x, %p and %%.

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void crt_puts(const char *message);
void crt_printf(const char *fmt, ...);
void crt_vprintf(const char *fmt, va_list args);

#ifdef __cplusplus
}
#endif
