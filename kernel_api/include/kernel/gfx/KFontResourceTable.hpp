#pragma once

#include <kernel/mutex.hpp>
#include <vector.hpp>

namespace kernel::font {

constexpr uint32_t MaxRegisteredBytes = 8 * 1024 * 1024;
constexpr uint32_t MaxProcessBytes = 8 * 1024 * 1024;
constexpr uint32_t ResourceSlots = 4;

class ResourceBudget {
public:
  bool reserve(uint32_t size) noexcept;
  void release(uint32_t size) noexcept;
  uint32_t used() const noexcept;
private:
  mutable Mutex mutex_;
  uint32_t bytes_{0};
};

enum class Result : uint32_t {
  Okay, InvalidHandle, InvalidSize, InvalidState, InvalidOffset, Limit,
  OutOfMemory, InvalidResource
};

// Separate handle namespace: high 24 bits generation, low byte slot+1.
// Retire exhausted generations rather than aliasing a stale resource handle.
class ResourceTable {
public:
  ResourceTable() = default;
  ~ResourceTable();
  ResourceTable(const ResourceTable &) = delete;
  ResourceTable &operator=(const ResourceTable &) = delete;

  Result begin(ResourceBudget &budget, uint32_t size, uint32_t &handle);
  Result append(uint32_t handle, uint32_t offset, const uint8_t *data, size_t size);
  Result commit(uint32_t handle, uint32_t &face, uint32_t &validationError);
  Result close(uint32_t handle);
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
