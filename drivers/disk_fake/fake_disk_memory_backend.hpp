#pragma once

// In-memory backing store, used by the freestanding i686-elf target (no host
// filesystem exists at kernel boot). Backed by a KE_Malloc'd raw buffer since
// kstd has no dynamic vector type available here.

#include "fake_disk.hpp"

#include <kernel/virtual_memory.hpp>

namespace diskfake {

class MemoryBackingStore final : public IBackingStore {
public:
  MemoryBackingStore() = default;
  ~MemoryBackingStore() override {
    KE_Free(data_);
  }

  MemoryBackingStore(const MemoryBackingStore &) = delete;
  MemoryBackingStore &operator=(const MemoryBackingStore &) = delete;

  [[nodiscard]] kstd::expected<size_t, kernel::KernelError> Size() const override { return size_; }

  kernel::KernelError Resize(size_t newSize) override {
    if (newSize == 0) {
      KE_Free(data_);
      data_ = nullptr;
      size_ = 0;
      return kernel::KernelError::Success;
    }
    auto *newData = static_cast<uint8_t *>(KE_Malloc(newSize));
    if (!newData) return kernel::KernelError::OutOfMemory;

    const size_t toCopy = newSize < size_ ? newSize : size_;
    for (size_t i = 0; i < toCopy; i++) newData[i] = data_[i];
    for (size_t i = toCopy; i < newSize; i++) newData[i] = 0;

    KE_Free(data_);
    data_ = newData;
    size_ = newSize;
    return kernel::KernelError::Success;
  }

  kernel::KernelError ReadAt(uint64_t offset, kstd::span<uint8_t> buffer) override {
    if (offset > size_ || buffer.size() > size_ - offset) return kernel::KernelError::OutOfRange;
    for (size_t i = 0; i < buffer.size(); i++) buffer[i] = data_[offset + i];
    return kernel::KernelError::Success;
  }

  kernel::KernelError WriteAt(uint64_t offset, kstd::span<const uint8_t> buffer) override {
    if (offset > size_ || buffer.size() > size_ - offset) return kernel::KernelError::OutOfRange;
    for (size_t i = 0; i < buffer.size(); i++) data_[offset + i] = buffer[i];
    return kernel::KernelError::Success;
  }

private:
  uint8_t *data_ = nullptr;
  size_t size_ = 0;
};

}// namespace diskfake
