#include <kernel/gfx/KFontResourceTable.hpp>
#include "face.hpp"
#include <undos/font/resource.hpp>
#include <mutex.hpp>

namespace kernel::font {

kstd::error_code ResourceBudget::reserve(uint32_t size) noexcept {
  kstd::unique_lock lock(mutex_);
  if (size > MaxRegisteredBytes - bytes_) return KernelError::ResourceLimitExceeded;
  bytes_ += size;
  return {};
}

void ResourceBudget::release(uint32_t size) noexcept {
  kstd::unique_lock lock(mutex_);
  bytes_ -= size;
}

uint32_t ResourceBudget::used() const noexcept {
  kstd::unique_lock lock(mutex_);
  return bytes_;
}

ResourceTable::~ResourceTable() { clear(); }

ResourceTable::Slot *ResourceTable::lookup(uint32_t handle) {
  const uint32_t index = handle & HandleSlotMask, generation = handle >> HandleSlotBits;
  if (!index || index > ResourceSlots || !generation) return nullptr;
  auto &slot = slots_[index - 1];
  return slot.used && slot.generation == generation ? &slot : nullptr;
}

kstd::expected<uint32_t, kstd::error_code> ResourceTable::begin(ResourceBudget &budget, uint32_t size) {
  if (size < undos::font::PayloadStart || size > undos::font::MaxResourceBytes || (size & ResourceSizeAlignmentMask))
    return kstd::unexpected(KernelError::InvalidSize);
  kstd::unique_lock lock(mutex_);
  if (size > MaxProcessBytes - bytes_) return kstd::unexpected(KernelError::ResourceLimitExceeded);
  for (uint32_t index = 0; index < ResourceSlots; ++index) {
    auto &slot = slots_[index];
    if (slot.used || !slot.generation) continue;
    if (const kstd::error_code reserved = budget.reserve(size); reserved)
      return kstd::unexpected(reserved);
    slot.data.resize(size);
    if (slot.data.size() != size || !slot.data.data()) {
      slot.data = kstd::vector<uint8_t>();
      budget.release(size);
      return kstd::unexpected(KernelError::OutOfMemory);
    }
    slot.used = true;
    slot.committed = false;
    slot.written = 0;
    slot.reserved = size;
    slot.budget = &budget;
    bytes_ += size;
    return (slot.generation << HandleSlotBits) | (index + 1);
  }
  return kstd::unexpected(KernelError::ResourceLimitExceeded);
}

kstd::error_code ResourceTable::append(uint32_t handle, uint32_t offset, const uint8_t *data, size_t size) {
  if (!data || !size || size > MaxAppendBytes) return KernelError::InvalidSize;
  kstd::unique_lock lock(mutex_);
  auto *slot = lookup(handle);
  if (!slot) return KernelError::InvalidHandle;
  if (slot->committed) return KernelError::InvalidState;
  if (offset != slot->written || size > slot->data.size() - slot->written) return KernelError::InvalidOffset;
  for (size_t i = 0; i < size; ++i) slot->data[offset + i] = data[i];
  slot->written += static_cast<uint32_t>(size);
  return {};
}

kstd::expected<uint32_t, kstd::error_code> ResourceTable::commit(uint32_t handle, FaceRegistry &registry,
                                                                 uint32_t &validationError) {
  validationError = static_cast<uint32_t>(undos::font::Error::None);
  kstd::unique_lock lock(mutex_);
  auto *slot = lookup(handle);
  if (!slot) return kstd::unexpected(KernelError::InvalidHandle);
  if (slot->committed || slot->written != slot->data.size()) return kstd::unexpected(KernelError::InvalidState);
  // Commit replaces the upload capability, invalidating its old generation.
  if (slot->generation == MaxGeneration) {
    release(*slot);
    return kstd::unexpected(KernelError::ResourceLimitExceeded);
  }
  const auto published = registry.publish(static_cast<kstd::vector<uint8_t> &&>(slot->data), validationError);
  if (!published) {
    release(*slot);
    return kstd::unexpected(published.error());
  }
  // The registry's budget now covers the bytes; the upload reservation ends.
  slot->budget->release(slot->reserved);
  slot->budget = nullptr;
  slot->registry = &registry;
  slot->faceHandle = *published;
  ++slot->generation;
  slot->committed = true;
  return (slot->generation << HandleSlotBits) | (handle & HandleSlotMask);
}

uint32_t ResourceTable::registry_handle(uint32_t handle) {
  kstd::unique_lock lock(mutex_);
  const auto *slot = lookup(handle);
  return slot && slot->committed ? slot->faceHandle : 0;
}

void ResourceTable::release(Slot &slot) {
  const uint32_t size = slot.reserved;
  slot.data = kstd::vector<uint8_t>();
  if (slot.budget) slot.budget->release(size);
  slot.budget = nullptr;
  if (slot.registry) (void) slot.registry->close(slot.faceHandle);
  slot.registry = nullptr;
  slot.faceHandle = 0;
  bytes_ -= size;
  slot.written = 0;
  slot.reserved = 0;
  slot.used = false;
  slot.committed = false;
  slot.generation = slot.generation == MaxGeneration ? 0 : slot.generation + 1;
}

kstd::error_code ResourceTable::close(uint32_t handle) {
  kstd::unique_lock lock(mutex_);
  auto *slot = lookup(handle);
  if (!slot) return KernelError::InvalidHandle;
  release(*slot);
  return {};
}

void ResourceTable::clear() {
  kstd::unique_lock lock(mutex_);
  for (auto &slot : slots_) if (slot.used) release(slot);
}

uint32_t ResourceTable::bytes() {
  kstd::unique_lock lock(mutex_);
  return bytes_;
}
}
