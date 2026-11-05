#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hal::x86 {

struct FrameOwnerSlot {
  uint32_t pid{0};
  uint64_t frames{0};
};

enum class FrameOwnerAssignment : uint8_t {
  Invalid,
  Assigned,
  AlreadyOwned,
  Shared,
};

inline bool bitmap_test(const uint32_t *bitmap, size_t frame) noexcept {
  return (bitmap[frame / 32] & (1u << (frame % 32))) != 0;
}

inline bool frame_available(size_t frame, const uint32_t *eligible,
                            const uint32_t *allocated,
                            const uint32_t *permanent) noexcept {
  return bitmap_test(eligible, frame) && !bitmap_test(allocated, frame) &&
      !bitmap_test(permanent, frame);
}

template <size_t FrameCapacity, size_t OwnerCapacity>
class FrameOwnerLedger {
 public:
  void reset() noexcept {
    for (size_t i = 0; i < FrameCapacity; ++i) frame_owner_[i] = 0;
    for (auto &owner : owners_) owner = {};
  }

  int find(uint32_t pid) const noexcept {
    for (size_t i = 1; i < OwnerCapacity; ++i)
      if (owners_[i].pid == pid) return static_cast<int>(i);
    return -1;
  }

  bool register_owner(uint32_t pid) noexcept {
    if (!pid || find(pid) >= 0) return false;
    for (size_t i = 1; i < OwnerCapacity; ++i) {
      if (!owners_[i].pid) {
        owners_[i] = {pid, 0};
        return true;
      }
    }
    return false;
  }

  bool unregister_owner(uint32_t pid) noexcept {
    const int slot = find(pid);
    if (slot < 0) return true;
    if (owners_[slot].frames) return false;
    owners_[slot] = {};
    return true;
  }

  bool query(uint32_t pid, uint64_t &frames) const noexcept {
    const int slot = find(pid);
    if (slot < 0) return false;
    frames = owners_[slot].frames;
    return true;
  }

  FrameOwnerAssignment assign(uint32_t pid, size_t frame,
                              bool eligible, bool allocated) noexcept {
    if (!pid) return FrameOwnerAssignment::Invalid;
    if (frame >= FrameCapacity || !eligible || !allocated)
      return FrameOwnerAssignment::Invalid;
    const int slot = find(pid);
    if (slot < 0) return FrameOwnerAssignment::Invalid;
    if (!frame_owner_[frame]) {
      frame_owner_[frame] = pid;
      ++owners_[slot].frames;
      return FrameOwnerAssignment::Assigned;
    }
    return frame_owner_[frame] == pid
        ? FrameOwnerAssignment::AlreadyOwned
        : FrameOwnerAssignment::Shared;
  }

  bool release(size_t frame) noexcept {
    if (frame >= FrameCapacity) return false;
    const uint32_t pid = frame_owner_[frame];
    if (!pid) return true;
    const int slot = find(pid);
    const bool valid = slot >= 0 && owners_[slot].frames;
    if (valid) --owners_[slot].frames;
    frame_owner_[frame] = 0;
    return valid;
  }

  uint32_t owner(size_t frame) const noexcept {
    return frame < FrameCapacity ? frame_owner_[frame] : 0;
  }

  bool validate_release(uint32_t pid, size_t start, size_t count,
                        size_t total_frames, const uint32_t *eligible,
                        const uint32_t *allocated,
                        const uint32_t *permanent) const noexcept {
    if (!eligible || !allocated || !permanent || !pid || !count ||
        start >= total_frames ||
        count > total_frames - start)
      return false;
    for (size_t i = 0; i < count; ++i) {
      const size_t frame = start + i;
      if (!bitmap_test(eligible, frame) || !bitmap_test(allocated, frame) ||
          bitmap_test(permanent, frame) || frame_owner_[frame] != pid)
        return false;
    }
    return true;
  }

  template <typename ReleaseFrame>
  bool release_range(uint32_t pid, size_t start, size_t count,
                     size_t total_frames, const uint32_t *eligible,
                     const uint32_t *allocated, const uint32_t *permanent,
                     ReleaseFrame &&release_frame) noexcept {
    if (!validate_release(pid, start, count, total_frames, eligible,
                          allocated, permanent))
      return false;
    for (size_t i = 0; i < count; ++i)
      release_frame(start + i);
    return true;
  }

  template <typename ReleaseFrame>
  bool release_region(uint32_t pid, uintptr_t base, size_t count,
                      size_t page_size, size_t total_frames,
                      const uint32_t *eligible, const uint32_t *allocated,
                      const uint32_t *permanent,
                      ReleaseFrame &&release_frame) noexcept {
    if (!page_size || base % page_size) return false;
    return release_range(pid, base / page_size, count, total_frames,
                         eligible, allocated, permanent,
                         static_cast<ReleaseFrame &&>(release_frame));
  }

 private:
  uint32_t frame_owner_[FrameCapacity]{};
  FrameOwnerSlot owners_[OwnerCapacity]{};
};

template <typename FreeFrame>
bool reclaim_owned_mapping(uint32_t owner, bool present, bool user,
                           uintptr_t frame, FreeFrame &&free_frame) noexcept {
  if (!owner || !present || !user) return true;
  return free_frame(owner, frame);
}

}  // namespace hal::x86
