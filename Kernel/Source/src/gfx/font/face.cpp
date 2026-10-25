#include "face.hpp"

#include <mutex.hpp>

namespace kernel::font {
namespace {
using undos::font::read_u32;
using undos::font::Section;

constexpr uint32_t GlyphCountAt = 20;
constexpr uint32_t UnitsPerEmAt = 24;
constexpr uint32_t GlyphProgramSizeAt = 56;
constexpr uint32_t LimitWords = sizeof(FaceLimits) / sizeof(uint32_t);
} // namespace

kstd::expected<Ref<Face>, kstd::error_code> Face::create(kstd::vector<uint8_t> &&bytes, ResourceBudget &budget,
                                                         uint32_t &validationError) {
  validationError = static_cast<uint32_t>(undos::font::Error::None);
  const auto size = to_u32(bytes.size());
  const auto error = undos::font::validate({.data = bytes.data(), .size = bytes.size()});
  validationError = static_cast<uint32_t>(error);
  if (error != undos::font::Error::None) return kstd::unexpected(KernelError::InvalidResource);
  if (const kstd::error_code reserved = budget.reserve(size); reserved) return kstd::unexpected(reserved);
  auto face = make_ref<Face>(static_cast<kstd::vector<uint8_t> &&>(bytes), budget, size);
  if (!face) {
    budget.release(size);
    return kstd::unexpected(KernelError::OutOfMemory);
  }
  return face;
}

Face::Face(kstd::vector<uint8_t> &&bytes, ResourceBudget &budget, uint32_t reserved)
    : bytes_(static_cast<kstd::vector<uint8_t> &&>(bytes)), budget_(budget), reserved_(reserved) {
  const auto resource = view();
  glyphCount_ = read_u32(resource.bytes().data + GlyphCountAt);
  unitsPerEm_ = read_u32(resource.bytes().data + UnitsPerEmAt);
  const auto limits = resource.section(Section::Limits);
  auto *words = reinterpret_cast<uint32_t *>(&limits_);
  for (uint32_t i = 0; i < LimitWords; ++i) words[i] = read_u32(limits.data + i * sizeof(uint32_t));

  uint32_t longest = static_cast<uint32_t>(resource.section(Section::FontProgram).size);
  const auto prep = static_cast<uint32_t>(resource.section(Section::PrepProgram).size);
  if (prep > longest) longest = prep;
  const auto glyphs = resource.section(Section::Glyphs);
  for (uint32_t i = 0; i < glyphCount_; ++i) {
    const uint32_t program = read_u32(glyphs.data + i * undos::font::GlyphSize + GlyphProgramSizeAt);
    if (program > longest) longest = program;
  }
  maxProgramBytes_ = longest;
}

Face::~Face() { budget_.release(reserved_); }

uint32_t Face::glyphFor(uint32_t codepoint) const {
  const auto cmap = view().section(Section::Cmap);
  // Validation guarantees sorted, non-overlapping ranges.
  size_t low = 0, high = cmap.size / undos::font::CmapSize;
  while (low < high) {
    const size_t mid = low + (high - low) / 2;
    const auto *range = cmap.data + mid * undos::font::CmapSize;
    const uint32_t first = read_u32(range), last = read_u32(range + 4);
    if (codepoint < first) high = mid;
    else if (codepoint > last) low = mid + 1;
    else return read_u32(range + 8) + (codepoint - first);
  }
  return 0;
}

FaceRegistry::~FaceRegistry() {
  for (auto &slot : slots_) slot.face.reset();
}

FaceRegistry::Slot *FaceRegistry::lookup(uint32_t handle) {
  const uint32_t index = handle & HandleSlotMask, generation = handle >> HandleSlotBits;
  if (!index || index > Slots || !generation) return nullptr;
  auto &slot = slots_[index - 1];
  return slot.face && slot.generation == generation ? &slot : nullptr;
}

kstd::expected<uint32_t, kstd::error_code> FaceRegistry::publish(kstd::vector<uint8_t> &&bytes,
                                                                 uint32_t &validationError) {
  auto face = Face::create(static_cast<kstd::vector<uint8_t> &&>(bytes), budget_, validationError);
  if (!face) return kstd::unexpected(face.error());
  kstd::unique_lock lock(mutex_);
  for (uint32_t index = 0; index < Slots; ++index) {
    auto &slot = slots_[index];
    if (slot.face || !slot.generation) continue;
    const uint32_t handle = (slot.generation << HandleSlotBits) | (index + 1);
    (*face)->identity_ = handle;
    slot.face = *face;
    return handle;
  }
  return kstd::unexpected(KernelError::ResourceLimitExceeded);
}

Ref<Face> FaceRegistry::acquire(uint32_t handle) {
  kstd::unique_lock lock(mutex_);
  const auto *slot = lookup(handle);
  return slot ? slot->face : Ref<Face>();
}

kstd::error_code FaceRegistry::close(uint32_t handle) {
  Ref<Face> released;
  {
    kstd::unique_lock lock(mutex_);
    auto *slot = lookup(handle);
    if (!slot) return KernelError::InvalidHandle;
    released = static_cast<Ref<Face> &&>(slot->face);
    slot->generation = slot->generation == MaxGeneration ? 0 : slot->generation + 1;
  }
  return {};
}

} // namespace kernel::font
