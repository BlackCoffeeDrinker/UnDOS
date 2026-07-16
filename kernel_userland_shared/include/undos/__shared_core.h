
#pragma once
#include <stdint.h>


#ifdef __cplusplus
#define NO_DISCARD [[nodiscard]]
#define INLINE inline
#else
#define NO_DISCARD
#define INLINE inline
#endif

#define UNDOS_SYSCALL_DEF NO_DISCARD INLINE
