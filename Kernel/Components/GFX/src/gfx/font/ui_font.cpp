#include <kernel/gfx/ui_font.hpp>

#include "font_system.hpp"
#include "gfx/bitmap_font.hpp"
#include <new.hpp>

namespace kernel::font {

static_assert(sizeof(UiFont) <= UiFontHandle::kStorageSize);
static_assert(alignof(UiFont) <= alignof(void *));
static_assert(kUiFirstCodepoint == UiGlyphs::FirstCodepoint);
static_assert(kUiCodepointCount == UiGlyphs::Count);
static_assert(kUiMenuVerticalPadding == UiGlyphs::MenuVerticalPadding);

namespace {

// Built-in bitmap glyphs as 1-bit masks: each 5x7 source pixel becomes one
// column (offset by 1, as DrawBitmapText draws) and two rows.
constexpr uint32_t kBitmapMaskWidth = 6;
constexpr uint32_t kBitmapMaskHeight = 14;
constexpr uint32_t kBitmapGlyphCount = gfx::kFontLastChar - gfx::kFontFirstChar + 1;

struct BitmapMasks {
  uint8_t rows[kBitmapGlyphCount][kBitmapMaskHeight];
};

constexpr BitmapMasks make_bitmap_masks() {
  BitmapMasks masks{};
  for (uint32_t g = 0; g < kBitmapGlyphCount; ++g)
    for (uint32_t row = 0; row < kBitmapMaskHeight; ++row)
      masks.rows[g][row] = static_cast<uint8_t>(gfx::kBitmapGlyphs[g][row / 2] << 2);
  return masks;
}

constexpr BitmapMasks kBitmapMasks = make_bitmap_masks();

int32_t bitmap_height() { return static_cast<int32_t>(gfx::kFontCellHeight); }

} // namespace

UiFontHandle::UiFontHandle() noexcept { new (storage_) UiFont(); }
UiFontHandle::UiFontHandle(const UiFontHandle &other) noexcept { new (storage_) UiFont(other.get()); }
UiFontHandle::UiFontHandle(UiFontHandle &&other) noexcept {
  new (storage_) UiFont(static_cast<UiFont &&>(other.get()));
}
UiFontHandle &UiFontHandle::operator=(const UiFontHandle &other) noexcept {
  if (this != &other) get() = other.get();
  return *this;
}
UiFontHandle &UiFontHandle::operator=(UiFontHandle &&other) noexcept {
  if (this != &other) get() = static_cast<UiFont &&>(other.get());
  return *this;
}
UiFontHandle::~UiFontHandle() { get().~UiFont(); }

UiFont &UiFontHandle::get() noexcept { return *reinterpret_cast<UiFont *>(storage_); }
const UiFont &UiFontHandle::get() const noexcept { return *reinterpret_cast<const UiFont *>(storage_); }

UiFontHandle UiFontHandle::adopt(UiFont &&font) noexcept {
  UiFontHandle handle;
  handle.get() = static_cast<UiFont &&>(font);
  return handle;
}

UiFontHandle UiFontHandle::snapshot_default() noexcept {
  UiFontHandle handle;
  if (auto *system = font_system_if_ready()) handle.get() = system->defaults.snapshot();
  return handle;
}

UiFontHandle::operator bool() const noexcept { return static_cast<bool>(get()); }
bool UiFontHandle::has_glyphs() const noexcept { return static_cast<bool>(get().ascii); }
uint32_t UiFontHandle::generation() const noexcept { return get().generation; }
int32_t UiFontHandle::text_height(uint32_t style) const noexcept {
  if (!get().ascii) return bitmap_height();
  const int32_t height = get().ascii->textHeight[style ? 1 : 0];
  return height > bitmap_height() ? height : bitmap_height();
}
int32_t UiFontHandle::text_above(uint32_t style) const noexcept {
  if (!get().ascii) return static_cast<int32_t>(gfx::kFontAscent);
  return get().ascii->textAbove[style ? 1 : 0];
}

void UiFontHandle::prewarm(const char *text, size_t length, uint32_t style) const noexcept {
  if (get().ascii) (void)get().ascii->prewarm(text, length, style ? 1 : 0);
}

bool UiFontHandle::all_ready(const char *text, size_t length, uint32_t style) const noexcept {
  return get().ascii && get().ascii->all_ready(text, length, style ? 1 : 0);
}

bool UiFontHandle::ready_glyphs(const char *text, size_t length, uint32_t style, UiGlyph *out,
                                bool preferVector) const noexcept {
  if (preferVector && get() && get().ascii) {
    constexpr size_t kChunk = 16;
    const GlyphMask *masks[kChunk];
    bool ready = true;
    for (size_t at = 0; ready && at < length; at += kChunk) {
      const size_t n = length - at < kChunk ? length - at : kChunk;
      ready = get().ascii->ready_masks(style ? 1 : 0, text + at, n, masks);
      for (size_t i = 0; ready && i < n; ++i) {
        const GlyphMask &m = *masks[i];
        out[at + i] = {m.left, m.top, m.width, m.height, m.stride, m.advance, m.bits.data()};
      }
    }
    if (ready) return true;
  }
  // Bitmap cell centred vertically in the line box, as the old USER fallback did.
  const int32_t top = text_above(style) - (text_height(style) - bitmap_height()) / 2;
  for (size_t i = 0; i < length; ++i) {
    auto ch = static_cast<uint8_t>(text[i]);
    if (ch < gfx::kFontFirstChar || ch > gfx::kFontLastChar) ch = gfx::kFontDefaultChar;
    out[i] = {0, top, kBitmapMaskWidth, kBitmapMaskHeight, 1,
              static_cast<int32_t>(gfx::kFontCellWidth), kBitmapMasks.rows[ch - gfx::kFontFirstChar]};
  }
  return false;
}

DefaultFont *system_default_font() noexcept {
  auto *system = font_system();
  return system ? &system->defaults : nullptr;
}

kstd::error_code publish_default(DefaultFont &defaults, uint32_t regularHandle, uint32_t boldHandle,
                                 uint32_t ppem, UiPublishCallback callback, void *context) noexcept {
  return defaults.publish(regularHandle, boldHandle, ppem, callback, context);
}

kstd::error_code publish_builtin_default(DefaultFont &defaults, UiPublishCallback callback,
                                         void *context) noexcept {
  return defaults.publish_builtin(callback, context);
}

void commit_default(DefaultFont &defaults, UiFontHandle &&candidate, UiFontHandle &displaced) noexcept {
  defaults.commit(static_cast<UiFont &&>(candidate.get()), displaced.get());
}

} // namespace kernel::font
