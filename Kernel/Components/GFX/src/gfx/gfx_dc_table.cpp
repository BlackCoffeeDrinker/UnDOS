#include "gfx/KDeviceContextTable.hpp"
#include "stdkrn.hpp"

namespace kernel::gfx {
namespace {
// File scope, not function-local: a local static would emit atexit, which the kernel does not export.
kstd::array<KDeviceContextSlot, kMaxDeviceContexts> g_slots;

Mutex g_lock;// guards g_slots metadata, g_free_index, g_next_index and the GFX process table
uint32_t g_free_index = UINT32_MAX;
uint32_t g_next_index = 0;

// Caller holds g_lock and the slot is live.
void close_locked(uint32_t index, DetachedDC &released) noexcept {
  auto &slot = g_slots[index];
  slot.used = false;
  
  if (slot.generation == UINT16_MAX || !slot.ChangeRenderState())
    slot.renderRetired = true;
  else
    ++slot.generation;
  
  released.target = kstd::move(slot.target);
  released.font = kstd::move(slot.selectedFont);
  slot.clipRects = nullptr;
  slot.clipCount = 0;
  slot.clipped = false;
  slot.hwnd = 0;
  slot.owner = {};
  
  if (!slot.renderRetired) {
    slot.nextFree = g_free_index;
    g_free_index = index;
  }
}
}// namespace

kstd::expected<kstd::pair<KDeviceContextSlot *, HDC>, kstd::error_code> AllocateDC(ProcessKey owner) noexcept {
  kstd::lock_guard guard(g_lock);
  uint32_t index = g_free_index;

  if (index == UINT32_MAX) {
    index = g_next_index;
    if (index >= kMaxDeviceContexts)
      return kstd::unexpected(KernelError::ResourceLimitExceeded);
    ++g_next_index;
  } else {
    g_free_index = g_slots[index].nextFree;
  }

  auto &slot = g_slots[index];
  slot.used = true;
  slot.nextFree = UINT32_MAX;
  slot.owner = owner;
  slot.penColor = 0;
  slot.backgroundColor = 7;
  slot.opaqueText = slot.memoryDC = slot.noAccel = false;
  slot.presentationDirty = false;
  slot.presentationBounds = {};
  slot.currentX = slot.currentY = slot.originX = slot.originY = 0;
  slot.clipped = false;
  slot.clipRects = nullptr;
  slot.clipCount = 0;
  slot.hwnd = 0;
  const HDC hdc = (static_cast<uint32_t>(slot.generation) << 16) | index;
  return kstd::make_pair(&slot, hdc);
}

KDeviceContextSlot *ResolveDC(HDC hdc) noexcept {
  const uint32_t index = hdc & UINT16_MAX;
  kstd::lock_guard guard(g_lock);
  auto *slot = index < g_next_index ? &g_slots[index] : nullptr;
  if (slot && (!slot->used || slot->renderRetired || slot->generation != (hdc >> 16)))
    slot = nullptr;
  return slot;
}

kstd::error_code CloseDCSlot(HDC hdc, DetachedDC &released) noexcept {
  const uint32_t index = hdc & UINT16_MAX;
  kstd::lock_guard guard(g_lock);

  const auto *slot = index < g_next_index ? &g_slots[index] : nullptr;
  if (!slot || !slot->used || slot->generation != (hdc >> 16))
    return KernelError::InvalidHandle;
  close_locked(index, released);

  return {};
}

size_t CloseAllOwnedBy(ProcessKey owner) noexcept {
  if (owner == kSystemOwner)
    return 0;
  size_t closed = 0;
  for (uint32_t index = 0;;) {
    DetachedDC released;// declared first: dropped after the lock is released
    kstd::lock_guard guard(g_lock);
    while (index < g_next_index && !(g_slots[index].used && g_slots[index].owner == owner))
      ++index;
    if (index >= g_next_index)
      return closed;
    close_locked(index++, released);
    ++closed;
  }
}
}// namespace kernel::gfx
