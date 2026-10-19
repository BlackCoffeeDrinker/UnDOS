#include <kernel/gfx/KFontResourceTable.hpp>
#include <undos/font/resource.hpp>
#include <mutex.hpp>

namespace kernel::font {

bool ResourceBudget::reserve(uint32_t size) noexcept {
  kstd::unique_lock lock(mutex_);
  if (size > MaxRegisteredBytes - bytes_) return false;
  bytes_ += size;
  return true;
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
  const uint32_t index = handle & 255u, generation = handle >> 8;
  if (!index || index > ResourceSlots || !generation) return nullptr;
  auto &slot = slots_[index - 1];
  return slot.used && slot.generation == generation ? &slot : nullptr;
}

Result ResourceTable::begin(ResourceBudget &budget, uint32_t size, uint32_t &handle) {
  handle = 0;
  if (size < undos::font::PayloadStart || size > undos::font::MaxResourceBytes || (size & 3u))
    return Result::InvalidSize;
  kstd::unique_lock lock(mutex_);
  if (size > MaxProcessBytes - bytes_) return Result::Limit;
  for (uint32_t index = 0; index < ResourceSlots; ++index) {
    auto &slot = slots_[index];
    if (slot.used || !slot.generation) continue;
    if (!budget.reserve(size)) return Result::Limit;
    slot.data.resize(size);
    if (slot.data.size() != size || !slot.data.data()) {
      slot.data = kstd::vector<uint8_t>();
      budget.release(size);
      return Result::OutOfMemory;
    }
    slot.used = true;
    slot.committed = false;
    slot.written = 0;
    slot.reserved = size;
    slot.budget = &budget;
    bytes_ += size;
    handle = (slot.generation << 8) | (index + 1);
    return Result::Okay;
  }
  return Result::Limit;
}

Result ResourceTable::append(uint32_t handle, uint32_t offset, const uint8_t *data, size_t size) {
  if (!data || !size || size > 4096) return Result::InvalidSize;
  kstd::unique_lock lock(mutex_);
  auto *slot = lookup(handle);
  if (!slot) return Result::InvalidHandle;
  if (slot->committed) return Result::InvalidState;
  if (offset != slot->written || size > slot->data.size() - slot->written) return Result::InvalidOffset;
  for (size_t i = 0; i < size; ++i) slot->data[offset + i] = data[i];
  slot->written += static_cast<uint32_t>(size);
  return Result::Okay;
}

Result ResourceTable::commit(uint32_t handle, uint32_t &face, uint32_t &validationError) {
  face = 0;
  validationError = static_cast<uint32_t>(undos::font::Error::None);
  kstd::unique_lock lock(mutex_);
  auto *slot = lookup(handle);
  if (!slot) return Result::InvalidHandle;
  if (slot->committed || slot->written != slot->data.size()) return Result::InvalidState;
  const auto error = undos::font::validate({slot->data.data(), slot->data.size()});
  validationError = static_cast<uint32_t>(error);
  if (error != undos::font::Error::None) {
    release(*slot);
    return Result::InvalidResource;
  }
  // Commit replaces the upload capability, invalidating its old generation.
  if (slot->generation == 0xffffffu) {
    release(*slot);
    return Result::Limit;
  }
  ++slot->generation;
  slot->committed = true;
  face = (slot->generation << 8) | (handle & 255u);
  return Result::Okay;
}

void ResourceTable::release(Slot &slot) {
  const uint32_t size = slot.reserved;
  slot.data = kstd::vector<uint8_t>();
  slot.budget->release(size);
  slot.budget = nullptr;
  bytes_ -= size;
  slot.written = 0;
  slot.reserved = 0;
  slot.used = false;
  slot.committed = false;
  slot.generation = slot.generation == 0xffffffu ? 0 : slot.generation + 1;
}

Result ResourceTable::close(uint32_t handle) {
  kstd::unique_lock lock(mutex_);
  auto *slot = lookup(handle);
  if (!slot) return Result::InvalidHandle;
  release(*slot);
  return Result::Okay;
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
