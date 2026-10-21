#pragma once

// Freestanding heap for userland: a bump allocator that carves fresh
// blocks out of arenas obtained from MEM AllocatePages, backed by a
// free-list so freed blocks are reused before growing the arena further.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *crt_malloc(size_t size);
void crt_free(void *ptr);
void *crt_calloc(size_t count, size_t size);
void *crt_realloc(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif
