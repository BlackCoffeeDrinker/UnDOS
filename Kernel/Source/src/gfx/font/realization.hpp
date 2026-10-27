#pragma once

#include "face.hpp"
#include "glyph.hpp"
#include "raster.hpp"

#include <atomic.hpp>
#include <mutex.hpp>
#include <undos/font/scaler.hpp>

namespace kernel::font {

struct RealizationTestAccess;

// Largest mask a realization produces. A larger glyph fails explicitly.
constexpr size_t MaxMaskBytes = 64 * 1024;
// Largest scratch a realization may allocate. Larger faces fail explicitly.
constexpr size_t MaxRealizationBytes = 1024 * 1024;
// Headroom above maxp's stack depth for fpgm, prep and glyph programs.
constexpr uint32_t StackHeadroom = 32;

enum class HintPolicy : uint8_t { Unhinted = 0, Hinted = 1 };

// Immutable monochrome glyph shared with drawing code. Row 0 is the top row.
// left/top are pixels from the pen origin with y growing upward.
// advance is whole pixels. An empty glyph keeps its advance.
struct GlyphMask final : RefCounted {
  int32_t left{0};
  int32_t top{0};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t stride{0};
  int32_t advance{0};
  // At least one glyph program ran to completion for this mask.
  bool hinted{false};
  // Glyph programs that failed and were replaced by unhinted points.
  uint32_t fallbacks{0};
  kstd::vector<uint8_t> bits;

};

struct RealizationStats {
  uint32_t renders{0};
  uint32_t fallbacks{0};
};

// A face at one size and hinting policy. It owns the mutable interpreter state
// (CVT, storage, twilight zone) and all scratch, so executions are serialized
// and nothing is shared with other realizations. Construction runs the font
// and prep programs, which is slow: create realizations in preemptible context
// and never while holding USER's scene lock or a DC lock.
class Realization final : public RefCounted {
public:
  // Errors: InvalidArgument (ppem outside MinPpem..MaxPpem), ResourceLimitExceeded
  // (scratch above MaxRealizationBytes), OutOfMemory, or the glyph loader's error.
  static kstd::expected<Ref<Realization>, kstd::error_code> create(const Ref<Face> &face, uint32_t ppem,
                                                                   HintPolicy policy);

  Realization(const Ref<Face> &face, uint32_t ppem, HintPolicy policy);

  const Ref<Face> &face() const { return face_; }
  uint32_t ppem() const { return ppem_; }
  HintPolicy policy() const { return policy_; }
  // True when the font and prep programs ran and glyph programs may execute.
  bool hinting() const { return loader_.hinting(); }
  RealizationStats stats() const;

  // Pixel line extents from the face's source metrics.
  kstd::error_code lineMetrics(undos::font::PixelLineMetrics &metrics) const;

  // Every request locks this realization and loads and rasterizes the glyph.
  // Hinted policy runs eligible glyph programs; unhinted policy does not.
  // A failure returns no mask; completed loader state changes are not rolled back.
  kstd::expected<Ref<GlyphMask>, kstd::error_code> glyph(uint32_t glyphId);

private:
  friend struct RealizationTestAccess;

  bool buffers_ok() const;
  void wire_state();
  kstd::expected<Ref<GlyphMask>, kstd::error_code> render(uint32_t glyphId);

  Ref<Face> face_;
  uint32_t ppem_;
  HintPolicy policy_;
  Mutex mutex_;
  kstd::atomic<uint32_t> renders_{0}, fallbacks_{0};
  kstd::vector<int32_t> cvt_, storage_, stack_;
  kstd::vector<uint8_t> written_, boundaries_;
  kstd::vector<vm::Function> functions_;
  kstd::vector<vm::InstructionDef> instructionDefs_;
  kstd::vector<vm::Point> twilightOriginal_, twilightCurrent_, original_, current_;
  kstd::vector<uint8_t> twilightTouched_, touched_, onCurve_, onCurveBackup_;
  kstd::vector<uint32_t> contourEnds_;
  kstd::vector<raster::Edge> edges_;
  kstd::vector<raster::Crossing> crossings_;
  vm::State state_{};
  glyph::GlyphLoader loader_;
  kstd::error_code prepared_{};
};

} // namespace kernel::font
