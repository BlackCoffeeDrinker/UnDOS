#include "default_font.hpp"

namespace kernel::font {

kstd::expected<UiFont, kstd::error_code> DefaultFont::prepare(uint32_t regularHandle, uint32_t boldHandle,
                                                              uint32_t ppem) {
  const auto regularFace = faces_.acquire(regularHandle);
  const auto boldFace = faces_.acquire(boldHandle);
  if (!regularFace || !boldFace) return kstd::unexpected(KernelError::InvalidHandle);
  auto regular = Realization::create(regularFace, ppem, HintPolicy::Hinted, &cache_);
  if (!regular) return kstd::unexpected(regular.error());
  auto bold = Realization::create(boldFace, ppem, HintPolicy::Hinted, &cache_);
  if (!bold) return kstd::unexpected(bold.error());

  auto glyphs = make_ref<UiGlyphs>();
  if (!glyphs) return kstd::unexpected(KernelError::OutOfMemory);
  const Ref<Realization> *faces[2] = {&*regular, &*bold};
  for (uint32_t style = 0; style < 2; ++style) {
    Realization &realization = **faces[style];
    undos::font::PixelLineMetrics metrics{};
    if (const auto error = realization.lineMetrics(metrics); error) return kstd::unexpected(error);
    glyphs->ascent[style] = metrics.ascent;
    glyphs->descent[style] = metrics.descent;
    glyphs->lineGap[style] = metrics.lineGap;
    for (uint32_t i = 0; i < UiGlyphs::Count; ++i) {
      auto mask = realization.glyph(realization.face()->glyphFor(UiGlyphs::FirstCodepoint + i));
      if (!mask) return kstd::unexpected(mask.error());
      glyphs->masks[style][i] = *mask;
    }
    auto replacement = realization.glyph(realization.face()->glyphFor(0xfffd));
    if (!replacement) return kstd::unexpected(replacement.error());
    glyphs->terminalReplacement[style] = *replacement;
  }

  for (uint32_t style = 0; style < 2; ++style) {
    uint64_t inkAbove = 0, inkBelow = 0;
    for (const auto &mask : glyphs->masks[style]) {
      const int64_t top = mask->top;
      const int64_t below = static_cast<int64_t>(mask->height) - top;
      if (top > 0 && static_cast<uint64_t>(top) > inkAbove) inkAbove = static_cast<uint64_t>(top);
      if (below > 0 && static_cast<uint64_t>(below) > inkBelow) inkBelow = static_cast<uint64_t>(below);
    }
    const uint64_t textAbove = glyphs->ascent[style] > inkAbove ? glyphs->ascent[style] : inkAbove;
    const uint64_t textBelow = glyphs->descent[style] > inkBelow ? glyphs->descent[style] : inkBelow;
    const uint64_t metricHeight = static_cast<uint64_t>(glyphs->ascent[style]) +
                                 glyphs->descent[style] + glyphs->lineGap[style];
    const uint64_t safeHeight = textAbove + textBelow;
    const uint64_t textHeight = metricHeight > safeHeight ? metricHeight : safeHeight;
    if (textAbove > INT32_MAX || textBelow > INT32_MAX ||
        textHeight > INT32_MAX - 40)
      return kstd::unexpected(KernelError::OutOfRange);
    glyphs->textAbove[style] = static_cast<int32_t>(textAbove);
    glyphs->textBelow[style] = static_cast<int32_t>(textBelow);
    glyphs->textHeight[style] = static_cast<int32_t>(textHeight);
  }
  const UiFont previous = snapshot();
  UiFont candidate;
  candidate.regular = *regular;
  candidate.bold = *bold;
  candidate.ascii = glyphs;
  candidate.ppem = ppem;
  candidate.bitmapBackend = false;
  candidate.generation = previous.generation + 1;
  return candidate;
}

void DefaultFont::commit(UiFont &&candidate, UiFont &displaced) {
  {
    kstd::unique_lock lock(mutex_);
    displaced = static_cast<UiFont &&>(current_);
    current_ = static_cast<UiFont &&>(candidate);
  }
}

kstd::error_code DefaultFont::publish(uint32_t regularHandle, uint32_t boldHandle, uint32_t ppem) {
  return publish(regularHandle, boldHandle, ppem,
                 [](void *, DefaultFont &defaults, UiFont &&candidate) {
                   UiFont displaced;
                   defaults.commit(static_cast<UiFont &&>(candidate), displaced);
                   return kstd::error_code{};
                 },
                 nullptr);
}

kstd::error_code DefaultFont::publish(uint32_t regularHandle, uint32_t boldHandle, uint32_t ppem,
                                      PublishCallback callback, void *context) {
  if (!callback) return KernelError::InvalidArgument;
  kstd::unique_lock publishing(publishMutex_);
  auto candidate = prepare(regularHandle, boldHandle, ppem);
  if (!candidate) return candidate.error();
  return callback(context, *this, static_cast<UiFont &&>(*candidate));
}

kstd::error_code DefaultFont::publish_builtin(PublishCallback callback, void *context) {
  if (!callback) return KernelError::InvalidArgument;
  kstd::unique_lock publishing(publishMutex_);
  UiFont candidate;
  candidate.bitmapBackend = true;
  candidate.generation = snapshot().generation + 1;
  return callback(context, *this, static_cast<UiFont &&>(candidate));
}

UiFont DefaultFont::snapshot() const {
  kstd::unique_lock lock(mutex_);
  return current_;
}

} // namespace kernel::font
