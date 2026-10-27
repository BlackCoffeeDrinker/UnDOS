#include "realization.hpp"

namespace kernel::font {
namespace {
using undos::font::Section;

constexpr uint32_t HeaderAscentAt = 28;
constexpr uint32_t HeaderDescentAt = 32;
constexpr uint32_t HeaderLineGapAt = 36;

uint32_t point_capacity(const FaceLimits &limits) {
  const uint32_t points = limits.maxPoints > limits.maxCompositePoints ? limits.maxPoints : limits.maxCompositePoints;
  return points + glyph::PhantomPoints;
}

uint32_t stack_capacity(const FaceLimits &limits) { return limits.maxStackElements + StackHeadroom; }

// Scratch the realization allocates, used to reject oversized faces first.
size_t scratch_bytes(const Face &face) {
  const auto &limits = face.limits();
  const size_t points = point_capacity(limits);
  size_t total = points * (2 * sizeof(vm::Point) + 3 + sizeof(uint32_t));
  total += points * (raster::MaxCurveSegments * sizeof(raster::Edge) + sizeof(raster::Crossing));
  total += static_cast<size_t>(limits.maxTwilightPoints) * (2 * sizeof(vm::Point) + 1);
  total += static_cast<size_t>(limits.maxStorage) * (sizeof(int32_t) + 1);
  total += static_cast<size_t>(limits.maxFunctionDefs) * sizeof(vm::Function);
  total += static_cast<size_t>(limits.maxInstructionDefs) * sizeof(vm::InstructionDef);
  total += stack_capacity(limits) * sizeof(int32_t);
  total += face.maxProgramBytes() + size_t{1};
  total += face.view().section(Section::Cvt).size;
  return total;
}
} // namespace

kstd::expected<Ref<Realization>, kstd::error_code> Realization::create(const Ref<Face> &face, uint32_t ppem,
                                                                       HintPolicy policy) {
  if (!face || ppem < undos::font::MinPpem || ppem > undos::font::MaxPpem) return kstd::unexpected(KernelError::InvalidArgument);
  if (scratch_bytes(*face) > MaxRealizationBytes) return kstd::unexpected(KernelError::ResourceLimitExceeded);
  auto realization = make_ref<Realization>(face, ppem, policy);
  if (!realization) return kstd::unexpected(KernelError::OutOfMemory);
  if (!realization->buffers_ok()) return kstd::unexpected(KernelError::OutOfMemory);
  if (realization->prepared_) return kstd::unexpected(realization->prepared_);
  return realization;
}

Realization::Realization(const Ref<Face> &face, uint32_t ppem, HintPolicy policy)
    : face_(face), ppem_(ppem), policy_(policy),
      cvt_(face->view().section(Section::Cvt).size / sizeof(int32_t)),
      storage_(face->limits().maxStorage), stack_(stack_capacity(face->limits())),
      written_(face->limits().maxStorage), boundaries_(face->maxProgramBytes() + size_t{1}),
      functions_(face->limits().maxFunctionDefs), instructionDefs_(face->limits().maxInstructionDefs),
      twilightOriginal_(face->limits().maxTwilightPoints), twilightCurrent_(face->limits().maxTwilightPoints),
      original_(point_capacity(face->limits())), current_(point_capacity(face->limits())),
      twilightTouched_(face->limits().maxTwilightPoints), touched_(point_capacity(face->limits())),
      onCurve_(point_capacity(face->limits())), onCurveBackup_(point_capacity(face->limits())),
      contourEnds_(point_capacity(face->limits())),
      edges_(raster::edge_capacity(point_capacity(face->limits()))), crossings_(edges_.size()),
      loader_(face->view(), state_,
              {.original = {original_.data(), original_.size()}, .current = {current_.data(), current_.size()}, .touched = {touched_.data(), touched_.size()}, .onCurve = {onCurve_.data(), onCurve_.size()}, .onCurveBackup = {onCurveBackup_.data(), onCurveBackup_.size()}, .contourEnds = {contourEnds_.data(), contourEnds_.size()}, .stack = {stack_.data(), stack_.size()}, .boundaries = {boundaries_.data(), boundaries_.size()}}) {
  if (!buffers_ok()) return;
  wire_state();
  prepared_ = loader_.prepare(ppem_);
}

bool Realization::buffers_ok() const {
  const auto &limits = face_->limits();
  const size_t points = point_capacity(limits);
  return cvt_.size() == face_->view().section(Section::Cvt).size / sizeof(int32_t) &&
         storage_.size() == limits.maxStorage && written_.size() == limits.maxStorage &&
         stack_.size() == stack_capacity(limits) && boundaries_.size() == face_->maxProgramBytes() + size_t{1} &&
         functions_.size() == limits.maxFunctionDefs && instructionDefs_.size() == limits.maxInstructionDefs &&
         twilightOriginal_.size() == limits.maxTwilightPoints && twilightCurrent_.size() == limits.maxTwilightPoints &&
         twilightTouched_.size() == limits.maxTwilightPoints && original_.size() == points &&
         current_.size() == points && touched_.size() == points && onCurve_.size() == points &&
         onCurveBackup_.size() == points && contourEnds_.size() == points &&
         edges_.size() == raster::edge_capacity(points) && crossings_.size() == edges_.size();
}

void Realization::wire_state() {
  state_.storage = {storage_.data(), storage_.size()};
  state_.storageWritten = {written_.data(), written_.size()};
  state_.cvt = {cvt_.data(), cvt_.size()};
  state_.functions = {functions_.data(), functions_.size()};
  state_.instructionDefs = {instructionDefs_.data(), instructionDefs_.size()};
  state_.twilight = {.original = {twilightOriginal_.data(), twilightOriginal_.size()},
                     .current = {twilightCurrent_.data(), twilightCurrent_.size()},
                     .touched = {twilightTouched_.data(), twilightTouched_.size()}};
}

RealizationStats Realization::stats() const {
  return {.renders = renders_.load(), .fallbacks = fallbacks_.load()};
}

kstd::error_code Realization::lineMetrics(undos::font::PixelLineMetrics &metrics) const {
  const auto *header = face_->view().bytes().data;
  const undos::font::SourceLineMetrics source{.ascent = undos::font::read_i32(header + HeaderAscentAt),
                                              .descent = undos::font::read_i32(header + HeaderDescentAt),
                                              .lineGap = undos::font::read_i32(header + HeaderLineGapAt)};
  if (undos::font::scale_line_metrics(source, ppem_, face_->unitsPerEm(), metrics) != undos::font::ScaleError::None)
    return KernelError::InvalidResource;
  return {};
}

kstd::expected<Ref<GlyphMask>, kstd::error_code> Realization::glyph(uint32_t glyphId) {
  kstd::unique_lock lock(mutex_);
  return render(glyphId);
}

kstd::expected<Ref<GlyphMask>, kstd::error_code> Realization::render(uint32_t glyphId) {
  glyph::Outline outline;
  if (const auto error = loader_.load(glyphId, policy_ == HintPolicy::Hinted, outline); error)
    return kstd::unexpected(error);
  raster::Bounds bounds;
  if (const auto error = raster::measure(outline, bounds); error) return kstd::unexpected(error);
  const size_t bytes = raster::stride_for(bounds.width) * size_t{bounds.height};
  if (bytes > MaxMaskBytes) return kstd::unexpected(KernelError::ResourceLimitExceeded);
  auto mask = make_ref<GlyphMask>();
  if (!mask) return kstd::unexpected(KernelError::OutOfMemory);
  mask->bits.resize(bytes);
  if (mask->bits.size() != bytes) return kstd::unexpected(KernelError::OutOfMemory);
  raster::Bitmap bitmap;
  const raster::Workspace workspace{.edges = {edges_.data(), edges_.size()}, .crossings = {crossings_.data(), crossings_.size()}};
  if (const auto error = raster::render(outline, workspace, {mask->bits.data(), mask->bits.size()}, bitmap); error)
    return kstd::unexpected(error);
  mask->left = bitmap.left;
  mask->top = bitmap.top;
  mask->width = bitmap.width;
  mask->height = bitmap.height;
  mask->stride = bitmap.stride;
  mask->advance = outline.advance / vm::Pixel;
  mask->hinted = outline.hinted;
  mask->fallbacks = outline.fallbacks;
  renders_.fetch_add(1);
  fallbacks_.fetch_add(outline.fallbacks);
  return mask;
}

} // namespace kernel::font
