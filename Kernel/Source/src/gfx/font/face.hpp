#pragma once

#include "ref.hpp"

#include <expected.hpp>
#include <kernel/gfx/KFontResourceTable.hpp>
#include <undos/font/resource.hpp>
#include <vector.hpp>

namespace kernel::font {

// maxp values widened to uint32 by the UFT1 Limits section, in file order.
struct FaceLimits {
  uint32_t maxPoints, maxContours, maxCompositePoints, maxCompositeContours;
  uint32_t maxZones, maxTwilightPoints, maxStorage, maxFunctionDefs, maxInstructionDefs;
  uint32_t maxStackElements, maxSizeOfInstructions, maxComponentElements, maxComponentDepth;
};

// An immutable, validated font resource owned by the kernel. A face stays alive
// while any registry slot, realization, default-font configuration or drawing
// call holds a reference, so closing a handle never frees bytes still in use.
// The ResourceBudget given to create() must outlive the face.
class Face final : public RefCounted {
public:
  // Validates the exact bytes that the face then retains. Errors: InvalidResource
  // (validationError receives the undos::font::Error detail), ResourceLimitExceeded
  // (budget) and OutOfMemory.
  static kstd::expected<Ref<Face>, kstd::error_code> create(kstd::vector<uint8_t> &&bytes, ResourceBudget &budget,
                                                            uint32_t &validationError);

  Face(kstd::vector<uint8_t> &&bytes, ResourceBudget &budget, uint32_t reserved);
  ~Face();

  undos::font::ResourceView view() const { return undos::font::ResourceView({.data = bytes_.data(), .size = bytes_.size()}); }
  const FaceLimits &limits() const { return limits_; }
  uint32_t unitsPerEm() const { return unitsPerEm_; }
  uint32_t glyphCount() const { return glyphCount_; }
  uint32_t byteSize() const { return reserved_; }
  // The longest font, prep or glyph program. Execution buffers need one more byte.
  uint32_t maxProgramBytes() const { return maxProgramBytes_; }
  // Kernel-assigned handle, valid once the registry publishes the face; zero before.
  uint32_t identity() const { return identity_; }
  // Glyph for a Unicode code point, or glyph 0 (.notdef) when the cmap has no range for it.
  uint32_t glyphFor(uint32_t codepoint) const;

private:
  friend class FaceRegistry;
  kstd::vector<uint8_t> bytes_;
  ResourceBudget &budget_;
  uint32_t reserved_;
  FaceLimits limits_{};
  uint32_t unitsPerEm_{0};
  uint32_t glyphCount_{0};
  uint32_t maxProgramBytes_{0};
  uint32_t identity_{0};
};

// Kernel-global table of committed faces. Handles use the layout of
// ResourceTable (low byte slot + 1, upper 24 bits generation) and are retired
// rather than reused when a generation is exhausted. Names never identify
// faces. Face storage has its own budget. The registry must outlive its faces.
class FaceRegistry {
public:
  static constexpr uint32_t Slots = 16;

  FaceRegistry() = default;
  ~FaceRegistry();
  FaceRegistry(const FaceRegistry &) = delete;
  FaceRegistry &operator=(const FaceRegistry &) = delete;

  // Takes ownership of normalized bytes. Errors are those of Face::create,
  // plus ResourceLimitExceeded when no slot is free.
  kstd::expected<uint32_t, kstd::error_code> publish(kstd::vector<uint8_t> &&bytes, uint32_t &validationError);
  // Returns an empty reference for a stale, closed or unknown handle.
  Ref<Face> acquire(uint32_t handle);
  // Drops the registry's reference. Holders of other references keep the face.
  kstd::error_code close(uint32_t handle);
  uint32_t bytes() const { return budget_.used(); }

private:
  struct Slot {
    uint32_t generation{1};
    Ref<Face> face;
  };
  ResourceBudget budget_;
  Mutex mutex_;
  Slot slots_[Slots];
  Slot *lookup(uint32_t handle);
};

} // namespace kernel::font
