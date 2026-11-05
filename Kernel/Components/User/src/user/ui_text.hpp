#pragma once

#include "gfx/font/font_system.hpp"
#include "gfx/bitmap_font.hpp"

namespace kernel::user {

class UiText {
public:
  // Owns a copy of the default font: for callers outside the USER lock (e.g.
  // prewarm). Under the USER lock use menu_text_locked(), which borrows.
  static UiText snapshot_default() {
    UiText text;
    if (auto *system = font::font_system_if_ready()) text.own_ = system->defaults.snapshot();
    return text;
  }

  // Borrows `font` without copying it; it must outlive this UiText (USER
  // fonts such as g_menuFont are only replaced under the USER lock).
  static UiText of(const font::UiFont &font) {
    UiText text;
    text.font_ = &font;
    return text;
  }

  // Temporaries are owned, never borrowed.
  static UiText of(font::UiFont &&font) {
    UiText text;
    text.own_ = static_cast<font::UiFont &&>(font);
    return text;
  }

  UiText(const UiText &other) : own_(other.own_), font_(other.owns() ? &own_ : other.font_) {}
  UiText(UiText &&other) noexcept
      : own_(static_cast<font::UiFont &&>(other.own_)), font_(other.owns() ? &own_ : other.font_) {}
  UiText &operator=(const UiText &) = delete;
  UiText &operator=(UiText &&) = delete;

  void prewarm(const char *text, size_t length, bool bold = false) const {
    if (font_->ascii) (void)font_->ascii->prewarm(text, length, bold ? 1 : 0);
  }

  int32_t text_height(bool bold = false) const {
    const int32_t bitmapHeight = static_cast<int32_t>(gfx::kFontCellHeight);
    if (!font_->ascii) return bitmapHeight;
    const int32_t height = font_->ascii->textHeight[bold ? 1 : 0];
    return height > bitmapHeight ? height : bitmapHeight;
  }

  int32_t text_above(bool bold = false) const {
    if (!font_->ascii) return static_cast<int32_t>(gfx::kFontAscent);
    return font_->ascii->textAbove[bold ? 1 : 0];
  }

  int32_t row_height(bool bold = false) const {
    return text_height(bold) + 2 * font::UiGlyphs::MenuVerticalPadding;
  }

  bool uses_default_font(const char *text, size_t length, bool bold) const {
    if (!*font_ || !font_->ascii) return false;
    for (size_t i = 0; i < length; ++i) {
      const auto ch = static_cast<uint8_t>(text[i]);
      if (ch < font::UiGlyphs::FirstCodepoint ||
          ch >= font::UiGlyphs::FirstCodepoint + font::UiGlyphs::Count)
        return false;
    }
    return font_->ascii->all_ready(text, length, bold ? 1 : 0);
  }

  int32_t width(const char *text, size_t length, bool bold) const {
    int32_t total = 0;
    const font::GlyphMask *masks[kRun];
    for (size_t at = 0; at < length; at += kRun) {
      const size_t n = length - at < kRun ? length - at : kRun;
      if (!resolve(text + at, n, bold, masks))
        return static_cast<int32_t>(length) * static_cast<int32_t>(gfx::kFontCellWidth);
      for (size_t i = 0; i < n; ++i) total += masks[i]->advance;
    }
    return total;
  }

  size_t fit(const char *text, size_t length, bool bold, int32_t available) const {
    size_t count = 0;
    while (count < length && text[count]) ++count;
    while (count && width(text, count, bold) > available) --count;
    return count;
  }

  template <typename Fill>
  void draw(int32_t x, int32_t y, const char *text, size_t length, bool bold,
            uint32_t foreground, uint32_t background, bool opaque, Fill &&fill) const {
    if (!length) return;
    const int32_t lineHeight = text_height(bold);
    const font::GlyphMask *masks[kRun];
    // Short runs (the common case) resolve once, under one glyph-table lock.
    const bool shortRun = length <= kRun;
    if (shortRun ? !resolve(text, length, bold, masks) : !uses_default_font(text, length, bold)) {
      const int32_t bitmapY = y + (lineHeight - static_cast<int32_t>(gfx::kFontCellHeight)) / 2;
      if (opaque) fill(x, y, x + width(text, length, bold), y + lineHeight, background);
      gfx::DrawBitmapText(x, bitmapY, text, length, foreground, background, false, fill);
      return;
    }
    const int32_t baseline = y + text_above(bold);
    if (shortRun) {
      if (opaque) {
        int32_t total = 0;
        for (size_t i = 0; i < length; ++i) total += masks[i]->advance;
        fill(x, y, x + total, y + lineHeight, background);
      }
    } else if (opaque) {
      fill(x, y, x + width(text, length, bold), y + lineHeight, background);
    }
    int32_t pen = x;
    for (size_t at = 0; at < length; at += kRun) {
      const size_t n = length - at < kRun ? length - at : kRun;
      if (length > kRun && !resolve(text + at, n, bold, masks)) return;
      for (size_t i = 0; i < n; ++i) {
        const font::GlyphMask *glyph = masks[i];
        for (uint32_t row = 0; row < glyph->height; ++row) {
          const int32_t top = baseline - glyph->top + static_cast<int32_t>(row);
          const uint8_t *bits = glyph->bits.data() + static_cast<size_t>(row) * glyph->stride;
          for (uint32_t col = 0; col < glyph->width;) {
            if (!(bits[col / 8] & (0x80u >> (col % 8)))) {
              ++col;
              continue;
            }
            uint32_t end = col + 1;
            while (end < glyph->width && (bits[end / 8] & (0x80u >> (end % 8)))) ++end;
            const int32_t left = pen + glyph->left;
            fill(left + static_cast<int32_t>(col), top,
                 left + static_cast<int32_t>(end), top + 1, foreground);
            col = end;
          }
        }
        pen += glyph->advance;
      }
    }
  }

private:
  UiText() = default;
  bool owns() const { return font_ == &own_; }

  static constexpr size_t kRun = 64;

  bool resolve(const char *text, size_t length, bool bold, const font::GlyphMask **out) const {
    if (!*font_ || !font_->ascii) return false;
    return font_->ascii->ready_masks(bold ? 1 : 0, text, length, out);
  }

  font::UiFont own_;
  const font::UiFont *font_{&own_};
};

} // namespace kernel::user
