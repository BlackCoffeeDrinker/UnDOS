#include "stdkrn.hpp"

namespace kernel {

kstd::expected<kstd::pair<KDeviceContextSlot *, HDC>, kstd::error_code> DeviceContextTable::Allocate() noexcept {
  table_lock.lock();

  uint32_t index;
  if (!free_indices.empty()) {
    index = free_indices.front();
    free_indices.pop_front();
  } else {
    index = next_index;
    if (index > 0xFFFF) {
      table_lock.unlock();
      return kstd::unexpected(KernelError::ResourceLimitExceeded);
    }

    if (const size_t chunkIndex = index / kSlotsPerChunk;
        chunkIndex >= chunks.size()) {
      auto chunk = kstd::make_unique<Chunk>();
      if (!chunk) {
        table_lock.unlock();
        return kstd::unexpected(KernelError::OutOfMemory);
      }
      chunks.push_back(kstd::move(chunk));
    }

    ++next_index;
  }

  const size_t chunkIndex = index / kSlotsPerChunk;
  const size_t slotIndex = index % kSlotsPerChunk;
  KDeviceContextSlot &slot = (*chunks[chunkIndex])[slotIndex];

  slot.used = true;
  slot.display = nullptr;
  slot.target = nullptr;
  slot.selectedBitmap = nullptr;
  slot.penColor = 0;
  slot.backgroundColor = 7;
  slot.opaqueText = false;
  slot.memoryDC = false;
  slot.noAccel = false;
  slot.presentationDirty = false;
  slot.presentationBounds = {};
  slot.currentX = 0;
  slot.currentY = 0;
  slot.originX = 0;
  slot.originY = 0;
  slot.clipped = false;
  slot.clipRects = nullptr;
  slot.clipCount = 0;
  slot.clipOwner = nullptr;

  const HDC hdc = (static_cast<uint32_t>(slot.generation) << 16) | (index & 0xFFFF);

  table_lock.unlock();
  return kstd::make_pair(&slot, hdc);
}

KDeviceContextSlot *DeviceContextTable::Resolve(HDC hdc) noexcept {
  const uint32_t index = hdc & 0xFFFF;
  const uint32_t tag = (hdc >> 16) & 0xFFFF;

  const size_t chunkIndex = index / kSlotsPerChunk;
  const size_t slotIndex = index % kSlotsPerChunk;

  if (chunkIndex >= chunks.size()) {
    return nullptr;
  }

  KDeviceContextSlot &slot = (*chunks[chunkIndex])[slotIndex];
  if (!slot.used || slot.generation != tag) {
    return nullptr;
  }

  return &slot;
}

kstd::error_code DeviceContextTable::Close(HDC hdc) noexcept {
  const uint32_t index = hdc & 0xFFFF;

  KDeviceContextSlot *slot = Resolve(hdc);
  if (!slot) {
    return KernelError::InvalidHandle;
  }

  slot->used = false;
  slot->display = nullptr;
  slot->target = nullptr;
  slot->selectedBitmap = nullptr;
  slot->clipped = false;
  slot->clipRects = nullptr;
  slot->clipCount = 0;
  slot->clipOwner = nullptr;
  ++slot->generation;

  table_lock.lock();
  free_indices.push_back(index);
  table_lock.unlock();

  return {};
}

}// namespace kernel
