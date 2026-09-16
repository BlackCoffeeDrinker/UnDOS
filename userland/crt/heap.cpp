#include <undoscrt/heap.h>
#include <undoscrt/string.h>

#include "../userspace.h"
#include <undos/syscalls.h>

namespace {
constexpr size_t PAGE_SIZE = 4096;
constexpr size_t ARENA_CHUNK_SIZE = 64 * 1024;

constexpr size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

// A free block header sits right before every block handed out by
// crt_malloc. `next` links free blocks together; it is unused (and
// overwritten by the caller's data) while the block is allocated.
struct BlockHeader {
  size_t size;
  BlockHeader *next;
};

// Bump state for the arena currently being carved into fresh blocks.
uint32_t g_arena_cursor = 0;
uint32_t g_arena_end = 0;

BlockHeader *g_free_list = nullptr;

bool GrowArena(size_t min_size) {
  const size_t chunk_size = align_up(min_size > ARENA_CHUNK_SIZE ? min_size : ARENA_CHUNK_SIZE, PAGE_SIZE);

  uint32_t address = 0;
  if (UD_SYS_AllocatePagesV1(static_cast<uint32_t>(chunk_size), static_cast<MemoryProtectFlags>(MemoryProtectFlags_Read | MemoryProtectFlags_Write), &address) != SyscallReturnCode_OKAY) {
    return false;
  }

  g_arena_cursor = address;
  g_arena_end = address + static_cast<uint32_t>(chunk_size);
  return true;
}
}// namespace

extern "C" void *crt_malloc(size_t size) {
  if (size == 0) return nullptr;

  const size_t aligned_size = align_up(size, sizeof(void *));

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
  if (static_cast<size_t>(g_arena_end - g_arena_cursor) < needed) {
    if (!GrowArena(needed)) return nullptr;
  }

  auto *header = reinterpret_cast<BlockHeader *>(static_cast<uintptr_t>(g_arena_cursor));
  header->size = aligned_size;
  header->next = nullptr;
  g_arena_cursor += static_cast<uint32_t>(needed);

  return header + 1;
}

extern "C" void crt_free(void *ptr) {
  if (!ptr) return;

  auto *header = reinterpret_cast<BlockHeader *>(ptr) - 1;
  header->next = g_free_list;
  g_free_list = header;
}

extern "C" void *crt_calloc(size_t count, size_t size) {
  const size_t total = count * size;
  void *ptr = crt_malloc(total);
  if (ptr) memset(ptr, 0, total);
  return ptr;
}
