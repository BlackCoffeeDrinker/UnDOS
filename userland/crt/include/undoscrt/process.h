#pragma once

// Freestanding process-lifetime helpers for userland applications.

#ifdef __cplusplus
extern "C" {
#endif

/** Terminates the current process with `exitCode`; never returns. */
#ifdef __cplusplus
#define UNDOS_CRT_NORETURN [[noreturn]]
#else
#define UNDOS_CRT_NORETURN _Noreturn
#endif
UNDOS_CRT_NORETURN void crt_exit(int exitCode);
UNDOS_CRT_NORETURN void crt_immediate_exit(int exitCode);
UNDOS_CRT_NORETURN void exit(int exitCode);
UNDOS_CRT_NORETURN void _Exit(int exitCode);
int atexit(void (*function)(void));
extern char **environ;
#undef UNDOS_CRT_NORETURN

#ifdef __cplusplus
}
#endif
