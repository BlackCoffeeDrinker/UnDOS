#pragma once

#include <expected.hpp>
#include <kernel/kerror.hpp>
#include <kernel/mutex.hpp>
#include <vector.hpp>

namespace kernel::font {

constexpr uint32_t MaxRegisteredBytes = 8 * 1024 * 1024;
constexpr uint32_t MaxProcessBytes = 8 * 1024 * 1024;
constexpr uint32_t ResourceSlots = 4;

class ResourceBudget {
public:
  // Fails with ResourceLimitExceeded when the system-wide budget is exhausted.
  KernelError reserve(uint32_t size) noexcept;
  void release(uint32_t size) noexcept;
  uint32_t used() const noexcept;
private:
  mutable Mutex mutex_;
  uint32_t bytes_{0};
};

// Separate handle namespace: high 24 bits generation, low byte slot+1.
// Retire exhausted generations rather than aliasing a stale resource handle.
class ResourceTable {
public:
  ResourceTable() = default;
  ~ResourceTable();
  ResourceTable(const ResourceTable &) = delete;
  ResourceTable &operator=(const ResourceTable &) = delete;

  // Reserves an upload slot and returns its handle. Errors: InvalidSize,
  // ResourceLimitExceeded (process/system budget or slots), OutOfMemory.
  kstd::expected<uint32_t, KernelError> begin(ResourceBudget &budget, uint32_t size);
  // Errors: InvalidSize, InvalidHandle, InvalidState (already committed),
  // InvalidOffset (non-sequential or overflowing chunk).
  KernelError append(uint32_t handle, uint32_t offset, const uint8_t *data, size_t size);
  // Validates the upload and returns the committed face handle.
  // `validationError` always receives the undos::font::Error detail; a
  // validation failure returns InvalidResource and releases the upload.
  // Other errors: InvalidHandle, InvalidState, ResourceLimitExceeded
  // (handle generations exhausted).
  kstd::expected<uint32_t, KernelError> commit(uint32_t handle, uint32_t &validationError);
  KernelError close(uint32_t handle);
  void clear();
  uint32_t bytes();

private:
  struct Slot {
    uint32_t generation{1};
    uint32_t written{0};
    uint32_t reserved{0};
    bool used{false};
    bool committed{false};
    kstd::vector<uint8_t> data;
    ResourceBudget *budget{nullptr};
  };
  Slot slots_[ResourceSlots];
  Mutex mutex_;
  uint32_t bytes_{0};
  Slot *lookup(uint32_t handle);
  void release(Slot &slot);
};

} // namespace kernel::font
