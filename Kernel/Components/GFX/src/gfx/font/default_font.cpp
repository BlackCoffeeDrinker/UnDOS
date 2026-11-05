#include "default_font.hpp"
#include "gfx/bitmap_font.hpp"

namespace kernel::font {
namespace {

bool bitmap_fallback(const Realization &realization, uint32_t codepoint) {
  return codepoint >= 0x20 && codepoint != 0x7f &&
         realization.face()->glyphFor(codepoint) == 0;
}

void include_bitmap_ink(int64_t &inkLeft, int64_t &inkTop,
                        int64_t &inkRight, int64_t &inkBottom) {
  const uint8_t replacement = static_cast<uint8_t>(gfx::kFontDefaultChar);
  for (uint32_t row = 0; row < 7; ++row) {
    const uint8_t bits = gfx::BitmapGlyphRow(replacement, row);
    for (uint32_t column = 0; column < 5; ++column) {
      if (!(bits & (16u >> column))) continue;
      const int32_t glyphLeft = static_cast<int32_t>(column + 1);
      const int32_t glyphTop = static_cast<int32_t>(row * 2);
      if (glyphLeft < inkLeft) inkLeft = glyphLeft;
      if (glyphLeft + 1 > inkRight) inkRight = glyphLeft + 1;
      if (glyphTop < inkTop) inkTop = glyphTop;
      if (glyphTop + 2 > inkBottom) inkBottom = glyphTop + 2;
    }
  }
}

kstd::error_code summarize(UiGlyphs &glyphs, uint32_t style, uint32_t repertoire,
                           bool fallbackBitmap) {
  const Realization &realization = *glyphs.realizations[style];
  FontMetricsSummary summary{};
  summary.ascent = glyphs.ascent[style];
  summary.descent = glyphs.descent[style];
  summary.lineGap = glyphs.lineGap[style];
  int64_t above = summary.ascent, below = summary.descent;
  int64_t left = INT64_MAX, right = INT64_MIN, top = INT64_MAX, bottom = INT64_MIN;
  uint32_t cellWidth = 0;
  auto include = [&](uint32_t codepoint, const GlyphMeasurement &glyph) {
    if (fallbackBitmap && bitmap_fallback(realization, codepoint)) {
      constexpr int64_t glyphTop = gfx::kFontAscent;
      constexpr int64_t glyphHeight = gfx::kFontAscent;
      const int64_t glyphBelow = glyphHeight - glyphTop;
      if (glyphTop > above) above = glyphTop;
      if (glyphBelow > below) below = glyphBelow;
      if (gfx::kFontCellWidth > cellWidth) cellWidth = gfx::kFontCellWidth;
      include_bitmap_ink(left, top, right, bottom);
      return;
    }
    const int64_t glyphBelow = static_cast<int64_t>(glyph.height) - glyph.top;
    if (glyph.top > above) above = glyph.top;
    if (glyphBelow > below) below = glyphBelow;
    const int64_t inkLeft = glyph.left;
    const int64_t inkRight = inkLeft + glyph.width;
    const int64_t candidateLeft = inkLeft < 0 ? inkLeft : 0;
    const int64_t candidateRight = inkRight > glyph.advance ? inkRight : glyph.advance;
    const int64_t candidateWidth = candidateRight - candidateLeft;
    if (candidateWidth > cellWidth) cellWidth = static_cast<uint32_t>(candidateWidth);
    const int64_t inkTop = -static_cast<int64_t>(glyph.top);
    const int64_t inkBottom = inkTop + glyph.height;
    if (glyph.width && glyph.height) {
      if (glyph.left < left) left = glyph.left;
      if (static_cast<int64_t>(glyph.left) + glyph.width > right)
        right = static_cast<int64_t>(glyph.left) + glyph.width;
    }
    if (inkTop < top) top = inkTop;
    if (inkBottom > bottom) bottom = inkBottom;
  };
  for (uint32_t cp = 0x20; cp <= 0x7e; ++cp)
    include(cp, glyphs.measurements[style][cp - UiGlyphs::FirstCodepoint]);
  if (repertoire)
    include(0xfffd, glyphs.replacementMeasurements[style]);
  if (left == INT64_MAX) left = right = top = bottom = 0;
  const uint64_t metricHeight = static_cast<uint64_t>(summary.ascent) +
                                summary.descent + summary.lineGap;
  const uint64_t safeHeight = static_cast<uint64_t>(above) + static_cast<uint64_t>(below);
  const uint64_t height = metricHeight > safeHeight ? metricHeight : safeHeight;
  if (height > UINT32_MAX || above > UINT32_MAX || below > UINT32_MAX ||
      left < INT32_MIN || right > INT32_MAX || top < INT32_MIN || bottom > INT32_MAX)
    return KernelError::OutOfRange;
  summary.lineHeight = static_cast<uint32_t>(height);
  summary.baselineOffset = static_cast<uint32_t>(above);
  summary.cellWidth = cellWidth;
  summary.cellHeight = static_cast<uint32_t>(height);
  summary.inkLeft = static_cast<int32_t>(left);
  summary.inkTop = static_cast<int32_t>(top);
  summary.inkRight = static_cast<int32_t>(right);
  summary.inkBottom = static_cast<int32_t>(bottom);
  glyphs.summaries[style][repertoire][fallbackBitmap ? 1 : 0] = summary;
  return {};
}

} // namespace

kstd::error_code UiGlyphs::prewarm(const char *text, size_t length, uint32_t style) {
  if (style > 1 || (length && !text)) return KernelError::InvalidArgument;
  for (size_t i = 0; i < length; ++i) {
    const uint32_t codepoint = static_cast<uint8_t>(text[i]);
    if (codepoint < FirstCodepoint || codepoint >= FirstCodepoint + Count) continue;
    auto mask = glyph(style, codepoint);
    if (!mask) return mask.error();
  }
  return {};
}

kstd::expected<Ref<GlyphMask>, kstd::error_code> UiGlyphs::glyph(uint32_t style,
                                                                  uint32_t codepoint) {
  if (style > 1 || (codepoint < FirstCodepoint ||
                    (codepoint >= FirstCodepoint + Count && codepoint != 0xfffd)))
    return kstd::unexpected(KernelError::InvalidArgument);
  kstd::unique_lock lock(mutex_);
  if (codepoint == 0xfffd) {
    if (terminalReplacement[style]) return terminalReplacement[style];
    auto mask = realizations[style]->glyph(replacementGlyphIds[style]);
    if (!mask) return kstd::unexpected(mask.error());
    terminalReplacement[style] = *mask;
    return *mask;
  }
  const size_t index = codepoint - FirstCodepoint;
  if (masks[style][index]) return masks[style][index];
  auto mask = realizations[style]->glyph(glyphIds[style][index]);
  if (!mask) return kstd::unexpected(mask.error());
  masks[style][index] = *mask;
  return *mask;
}

bool UiGlyphs::all_ready(const char *text, size_t length, uint32_t style) const {
  if (style > 1 || (length && !text)) return false;
  kstd::unique_lock lock(mutex_);
  for (size_t i = 0; i < length; ++i) {
    const uint32_t codepoint = static_cast<uint8_t>(text[i]);
    if (codepoint < FirstCodepoint || codepoint >= FirstCodepoint + Count ||
        !masks[style][codepoint - FirstCodepoint]) return false;
  }
  return true;
}

bool UiGlyphs::slot_ready(uint32_t style, uint32_t codepoint) const {
  if (style > 1 || (codepoint < FirstCodepoint ||
                    (codepoint >= FirstCodepoint + Count && codepoint != 0xfffd))) return false;
  kstd::unique_lock lock(mutex_);
  if (codepoint == 0xfffd) return static_cast<bool>(terminalReplacement[style]);
  return static_cast<bool>(masks[style][codepoint - FirstCodepoint]);
}

Ref<GlyphMask> UiGlyphs::ready_glyph(uint32_t style, uint32_t codepoint) const {
  if (style > 1 || codepoint < FirstCodepoint || codepoint >= FirstCodepoint + Count) return {};
  kstd::unique_lock lock(mutex_);
  return masks[style][codepoint - FirstCodepoint];
}

bool UiGlyphs::ready_masks(uint32_t style, const char *text, size_t length, const GlyphMask **out) const {
  if (style > 1 || (length && (!text || !out))) return false;
  kstd::unique_lock lock(mutex_);
  for (size_t i = 0; i < length; ++i) {
    const uint32_t codepoint = static_cast<uint8_t>(text[i]);
    if (codepoint < FirstCodepoint || codepoint >= FirstCodepoint + Count) return false;
    const auto &mask = masks[style][codepoint - FirstCodepoint];
    if (!mask) return false;
    out[i] = mask.get();
  }
  return true;
}

kstd::expected<UiFont, kstd::error_code> DefaultFont::prepare(uint32_t regularHandle, uint32_t boldHandle,
                                                              uint32_t ppem) {
  const auto regularFace = faces_.acquire(regularHandle);
  const auto boldFace = faces_.acquire(boldHandle);
  if (!regularFace || !boldFace) return kstd::unexpected(KernelError::InvalidHandle);
  auto regular = Realization::create(regularFace, ppem, HintPolicy::Hinted);
  if (!regular) return kstd::unexpected(regular.error());
  auto bold = Realization::create(boldFace, ppem, HintPolicy::Hinted);
  if (!bold) return kstd::unexpected(bold.error());

  auto glyphs = make_ref<UiGlyphs>();
  if (!glyphs) return kstd::unexpected(KernelError::OutOfMemory);
  const Ref<Realization> *faces[2] = {&*regular, &*bold};
  for (uint32_t style = 0; style < 2; ++style) {
    Realization &realization = **faces[style];
    glyphs->realizations[style] = *faces[style];
    undos::font::PixelLineMetrics metrics{};
    if (const auto error = realization.lineMetrics(metrics); error) return kstd::unexpected(error);
    glyphs->ascent[style] = metrics.ascent;
    glyphs->descent[style] = metrics.descent;
    glyphs->lineGap[style] = metrics.lineGap;
    uint32_t measuredIds[UiGlyphs::Count + 1]{};
    GlyphMeasurement measuredValues[UiGlyphs::Count + 1]{};
    uint32_t measuredCount = 0;
    for (uint32_t i = 0; i < UiGlyphs::Count; ++i) {
      const uint32_t id = realization.face()->glyphFor(UiGlyphs::FirstCodepoint + i);
      glyphs->glyphIds[style][i] = id;
      uint32_t at = 0;
      while (at < measuredCount && measuredIds[at] != id) ++at;
      if (at == measuredCount) {
        if (const auto error = realization.measure(id, measuredValues[measuredCount]); error)
          return kstd::unexpected(error);
        measuredIds[measuredCount] = id;
        at = measuredCount++;
      }
      glyphs->measurements[style][i] = measuredValues[at];
    }
    const uint32_t replacementId = realization.face()->glyphFor(0xfffd);
    glyphs->replacementGlyphIds[style] = replacementId;
    uint32_t at = 0;
    while (at < measuredCount && measuredIds[at] != replacementId) ++at;
    if (at == measuredCount) {
      if (const auto error = realization.measure(replacementId, measuredValues[measuredCount]); error)
        return kstd::unexpected(error);
      measuredIds[measuredCount] = replacementId;
      at = measuredCount++;
    }
    glyphs->replacementMeasurements[style] = measuredValues[at];
  }

  for (uint32_t style = 0; style < 2; ++style) {
    uint64_t inkAbove = 0, inkBelow = 0;
    for (const auto &mask : glyphs->measurements[style]) {
      const int64_t top = mask.top;
      const int64_t below = static_cast<int64_t>(mask.height) - top;
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
    for (uint32_t repertoire = 0; repertoire < 2; ++repertoire)
      for (uint32_t fallback = 0; fallback < 2; ++fallback)
        if (const auto error = summarize(*glyphs, style, repertoire, fallback != 0); error)
          return kstd::unexpected(error);
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
