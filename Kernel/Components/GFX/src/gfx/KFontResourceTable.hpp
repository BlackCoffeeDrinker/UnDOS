#pragma once

#include <expected.hpp>
#include <kernel/kerror.hpp>
#include <kernel/mutex.hpp>
#include <kernel/virtual_memory.hpp>

namespace kernel::font {

class FaceRegistry;

// Contiguous UFT1 storage. Allocation failure leaves an empty owner; moves
// transfer the exact byte allocation without allocating or copying.
class FontResourceStorage {
public:
  FontResourceStorage() = default;
  ~FontResourceStorage() { KE_Free(data_); }
  FontResourceStorage(const FontResourceStorage &) = delete;
  FontResourceStorage &operator=(const FontResourceStorage &) = delete;
  FontResourceStorage(FontResourceStorage &&other) noexcept
      : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
  }
  FontResourceStorage &operator=(FontResourceStorage &&other) noexcept {
    if (this == &other) return *this;
    KE_Free(data_);
    data_ = other.data_;
    size_ = other.size_;
    other.data_ = nullptr;
    other.size_ = 0;
    return *this;
  }
  static FontResourceStorage allocate(size_t size) noexcept {
    FontResourceStorage storage;
    storage.data_ = static_cast<uint8_t *>(KE_Malloc(size));
    if (storage.data_) storage.size_ = size;
    return storage;
  }
  uint8_t *data() const noexcept { return data_; }
  size_t size() const noexcept { return size_; }
  explicit operator bool() const noexcept { return data_ != nullptr; }
private:
  uint8_t *data_{nullptr};
  size_t size_{0};
};

constexpr uint32_t MaxRegisteredBytes = 8 * 1024 * 1024;
constexpr uint32_t MaxProcessBytes = 8 * 1024 * 1024;
// System-wide font slots (max 255: the handle low byte holds slot + 1).
constexpr uint32_t ResourceSlots = 255;
constexpr uint32_t MaxAppendBytes = 4096;

// Handle layout: the low byte holds slot + 1 and the upper 24 bits hold the generation.
constexpr uint32_t HandleSlotBits = 8;
constexpr uint32_t HandleSlotMask = (1u << HandleSlotBits) - 1;
constexpr uint32_t MaxGeneration = ~uint32_t(0) >> HandleSlotBits;
static_assert(ResourceSlots <= HandleSlotMask, "font handle low byte holds slot + 1");
constexpr uint32_t ResourceSizeAlignmentMask = 3;

class ResourceBudget {
public:
  // Fails with ResourceLimitExceeded when the system-wide budget is exhausted.
  kstd::error_code reserve(uint32_t size) noexcept;
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
  kstd::expected<uint32_t, kstd::error_code> begin(ResourceBudget &budget, uint32_t size, uint32_t ownerPid = 0, uint64_t ownerIncarnation = 0);
  // Releases uploads begun by the owner and never committed; committed faces stay.
  size_t release_uncommitted(uint32_t ownerPid, uint64_t ownerIncarnation);
  // Errors: InvalidSize, InvalidHandle, InvalidState (already committed),
  // InvalidOffset (non-sequential or overflowing chunk).
  kstd::error_code append(uint32_t handle, uint32_t offset, const uint8_t *data, size_t size);
  // Moves the upload into `registry` and returns the process-local face handle.
  // The table keeps only that handle; the face bytes belong to the registry,
  // which must outlive this table. Closing the handle or clearing the table
  // drops the registry's reference, and holders of other references keep the face.
  // `validationError` always receives the undos::font::Error detail; a
  // validation failure returns InvalidResource and releases the upload.
  // Other errors: InvalidHandle, InvalidState, ResourceLimitExceeded
  // (registry slots, registry budget or handle generations), OutOfMemory.
  kstd::expected<uint32_t, kstd::error_code> commit(uint32_t handle, FaceRegistry &registry, uint32_t &validationError);
  // Returns the registry handle behind a committed face handle, or zero for an
  // upload in progress, a closed handle or an unknown handle.
  uint32_t registry_handle(uint32_t handle);
  kstd::error_code close(uint32_t handle);
  void clear();
  uint32_t bytes();

private:
  struct Slot {
    uint32_t generation{1};
    uint32_t written{0};
    uint32_t reserved{0};
    bool used{false};
    bool committed{false};
    FontResourceStorage data;
    ResourceBudget *budget{nullptr};
    FaceRegistry *registry{nullptr};
    uint32_t faceHandle{0};
    uint32_t ownerPid{0};
    uint64_t ownerIncarnation{0};
  };
  Slot slots_[ResourceSlots];
  Mutex mutex_;
  uint32_t bytes_{0};
  Slot *lookup(uint32_t handle);
  void release(Slot &slot);
};

} // namespace kernel::font
