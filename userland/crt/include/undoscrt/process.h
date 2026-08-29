#pragma once

// Freestanding process-lifetime helpers for userland applications.

#ifdef __cplusplus
extern "C" {
#endif

/** Terminates the current process with `exitCode`; never returns. */
[[noreturn]] void crt_exit(int exitCode);

#ifdef __cplusplus
}
#endif
