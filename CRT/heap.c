#include <undoscrt/heap.h>
#include <undoscrt/string.h>

#include "libc/backend.h"
#include <stdbool.h>

#define PAGE_SIZE 4096
#define ARENA_CHUNK_SIZE (64 * 1024)

static size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

// A free block header sits right before every block handed out by
// crt_malloc. `next` links free blocks together; it is unused (and
// overwritten by the caller's data) while the block is allocated.
typedef struct BlockHeader {
  _Alignas(16) size_t size;
  struct BlockHeader *next;
} BlockHeader;

// Bump state for the arena currently being carved into fresh blocks.
static uintptr_t g_arena_cursor = 0;
static uintptr_t g_arena_end = 0;

static BlockHeader *g_free_list = NULL;

static bool GrowArena(size_t min_size) {
  if (min_size > SIZE_MAX - (PAGE_SIZE - 1)) return false;
  const size_t chunk_size = align_up(min_size > ARENA_CHUNK_SIZE ? min_size : ARENA_CHUNK_SIZE, PAGE_SIZE);

  const uintptr_t address = (uintptr_t)undos_libc_allocate_pages(chunk_size);
  if (!address || address > UINTPTR_MAX - chunk_size) return false;
  g_arena_cursor = address;
  g_arena_end = address + chunk_size;
  return true;
}

void *crt_malloc(size_t size) {
  if (size == 0 || size > SIZE_MAX - sizeof(BlockHeader) - (_Alignof(BlockHeader) - 1)) return NULL;

  const size_t aligned_size = align_up(size, _Alignof(BlockHeader));

  // 1. First-fit search through the free list before touching the arena.
  BlockHeader **link = &g_free_list;
  while (*link) {
    if ((*link)->size >= aligned_size) {
      BlockHeader *block = *link;
      *link = block->next;
      return block + 1;
    }
    link = &(*link)->next;
  }

  // 2. Nothing reusable: bump-allocate a fresh block from the arena,
  // growing it (via MEM AllocatePages) if there isn't enough room left.
  const size_t needed = sizeof(BlockHeader) + aligned_size;
  if ((size_t)(g_arena_end - g_arena_cursor) < needed) {
    if (!GrowArena(needed)) return NULL;
  }

  BlockHeader *header = (BlockHeader *)g_arena_cursor;
  header->size = aligned_size;
  header->next = NULL;
  g_arena_cursor += needed;

  return header + 1;
}

void crt_free(void *ptr) {
  if (!ptr) return;

  BlockHeader *header = (BlockHeader *)ptr - 1;
  header->next = g_free_list;
  g_free_list = header;
}

void *crt_calloc(size_t count, size_t size) {
  if (size && count > SIZE_MAX / size) return NULL;
  const size_t total = count * size;
  void *ptr = crt_malloc(total);
  if (ptr) memset(ptr, 0, total);
  return ptr;
}

void *crt_realloc(void *ptr, size_t size) {
  if (!ptr) return crt_malloc(size);
  if (!size) {
    crt_free(ptr);
    return NULL;
  }
  const BlockHeader *header = (BlockHeader *)ptr - 1;
  if (size <= header->size) return ptr;
  void *replacement = crt_malloc(size);
  if (!replacement) return NULL;
  memcpy(replacement, ptr, header->size);
  crt_free(ptr);
  return replacement;
}
