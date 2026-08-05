#pragma once

#include <__memory/unique_ptr.hpp>
#include <__utility/pair.hpp>
#include <array.hpp>
#include <list.hpp>
#include <vector.hpp>

#include <kernel/__core.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/spinlock.hpp>

namespace kernel {
struct KDisplay;
struct KSurface;

// GDI-style device context handle: bits 0-15 are the slot index, bits 16-31
// are the slot's generation tag (bumped on every Close(), rejecting stale
// handles that alias a reused index).
using HDC = uint32_t;
constexpr HDC INVALID_HDC = 0;

// Ephemeral per-process device-context state: target of drawing primitives
// (Rectangle, LineTo, BitBlt, ...). Unlike KDisplay/KSurface (long-lived
// resources managed by the Object Manager), DC slots live only inside a
// process's DeviceContextTable and are never KObjects themselves.
struct KDeviceContextSlot {
  bool used = false;
  // Bumped every Close() to invalidate stale HDCs. Starts at 1 (not 0) so
  // the very first handle ever allocated for a given slot (index 0,
  // generation 1) never computes to INVALID_HDC (0).
  uint16_t generation = 1;
  SpinLock lock;// guards this slot's fields during draw calls

  KObjectPtr<KDisplay> display;      // target display (primary DC)
  KObjectPtr<KSurface> target;       // currently selected surface (display surface or a bitmap)
  KObjectPtr<KSurface> selectedBitmap;// last SelectObject'd bitmap, if any

  uint32_t penColor = 0;

  // MoveTo/LineTo cursor
  int32_t currentX = 0;
  int32_t currentY = 0;
};

// Dynamically-growing, chunked table of KDeviceContextSlots owned by a
// single KProcessObject. Grows one 64-slot chunk at a time (instead of a
// large static pre-allocated array) so low-memory (386/486-class) targets
// aren't forced to reserve memory for DCs a process never creates.
struct DeviceContextTable {
  static constexpr size_t kSlotsPerChunk = 64;
  using Chunk = kstd::array<KDeviceContextSlot, kSlotsPerChunk>;

  SpinLock table_lock;// guards chunk growth + free_indices/next_index
  kstd::vector<kstd::unique_ptr<Chunk>> chunks;
  kstd::list<uint32_t> free_indices;
  uint32_t next_index = 0;

  // Allocates a fresh slot, growing by one Chunk (one kernel::New
  // allocation) only when the free list is empty and every existing chunk
  // is full. Returns {slot pointer, HDC} or {nullptr, INVALID_HDC} on
  // allocation failure.
  kstd::pair<KDeviceContextSlot *, HDC> Allocate() noexcept;

  // Resolves a HDC to its slot. Bit-splits `index = hdc & 0xFFFF` /
  // `generation = (hdc >> 16) & 0xFFFF`; rejects an out-of-range index, an
  // unused slot, or a generation mismatch (stale handle).
  KDeviceContextSlot *Resolve(HDC hdc) noexcept;

  // Bumps the slot's generation (invalidating any outstanding HDC into it)
  // and returns its index to the free list. Returns false if `hdc` didn't
  // resolve to a live slot.
  bool Close(HDC hdc) noexcept;
};

}// namespace kernel
