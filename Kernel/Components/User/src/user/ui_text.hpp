#pragma once

#include <kernel/gfx/ui_font.hpp>

namespace kernel::user {

class UiText {
public:
  // Owns a copy of the default font: for callers outside the USER lock (e.g.
  // prewarm). Under the USER lock use menu_text_locked(), which borrows.
  static UiText snapshot_default() {
    UiText text;
    text.own_ = font::UiFontHandle::snapshot_default();
    return text;
  }

  // Borrows `font` without copying it; it must outlive this UiText (USER
  // fonts such as g_menuFont are only replaced under the USER lock).
  static UiText of(const font::UiFontHandle &font) {
    UiText text;
    text.font_ = &font;
    return text;
  }

  // Temporaries are owned, never borrowed.
  static UiText of(font::UiFontHandle &&font) {
    UiText text;
    text.own_ = static_cast<font::UiFontHandle &&>(font);
    return text;
  }

  UiText(const UiText &other) : own_(other.own_), font_(other.owns() ? &own_ : other.font_) {}
  UiText(UiText &&other) noexcept
      : own_(static_cast<font::UiFontHandle &&>(other.own_)), font_(other.owns() ? &own_ : other.font_) {}
  UiText &operator=(const UiText &) = delete;
  UiText &operator=(UiText &&) = delete;

  void prewarm(const char *text, size_t length, bool bold = false) const {
    font_->prewarm(text, length, bold ? 1 : 0);
  }

  int32_t text_height(bool bold = false) const { return font_->text_height(bold ? 1 : 0); }

  int32_t text_above(bool bold = false) const { return font_->text_above(bold ? 1 : 0); }

  int32_t row_height(bool bold = false) const {
    return text_height(bold) + 2 * font::kUiMenuVerticalPadding;
  }

  bool uses_default_font(const char *text, size_t length, bool bold) const {
    if (!*font_ || !font_->has_glyphs()) return false;
    for (size_t i = 0; i < length; ++i) {
      const auto ch = static_cast<uint8_t>(text[i]);
      if (ch < font::kUiFirstCodepoint ||
          ch >= font::kUiFirstCodepoint + font::kUiCodepointCount)
        return false;
    }
    return font_->all_ready(text, length, bold ? 1 : 0);
  }

  int32_t width(const char *text, size_t length, bool bold) const {
    int32_t total = 0;
    font::UiGlyph masks[kRun];
    const bool vector = length <= kRun || uses_default_font(text, length, bold);
    for (size_t at = 0; at < length; at += kRun) {
      const size_t n = length - at < kRun ? length - at : kRun;
      resolve(text + at, n, bold, masks, vector);
      for (size_t i = 0; i < n; ++i) total += masks[i].advance;
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
    font::UiGlyph masks[kRun];
    // Short runs (the common case) resolve once, under one glyph-table lock.
    // Long runs pick vector or bitmap glyphs once, so chunks never mix.
    const bool shortRun = length <= kRun;
    const bool vector = shortRun ? resolve(text, length, bold, masks, true) : uses_default_font(text, length, bold);
    const int32_t baseline = y + text_above(bold);
    if (opaque) {
      int32_t total = 0;
      if (shortRun)
        for (size_t i = 0; i < length; ++i) total += masks[i].advance;
      else
        total = width(text, length, bold);
      fill(x, y, x + total, y + lineHeight, background);
    }
    int32_t pen = x;
    for (size_t at = 0; at < length; at += kRun) {
      const size_t n = length - at < kRun ? length - at : kRun;
      if (!shortRun) resolve(text + at, n, bold, masks, vector);
      for (size_t i = 0; i < n; ++i) {
        const font::UiGlyph *glyph = &masks[i];
        for (uint32_t row = 0; row < glyph->height; ++row) {
          const int32_t top = baseline - glyph->top + static_cast<int32_t>(row);
          const uint8_t *bits = glyph->bits + static_cast<size_t>(row) * glyph->stride;
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

  // Menu labels (Win32 style): "&x" underlines x, "&&" is a literal '&', and
  // a '\t' starts the accelerator column, drawn right-aligned at `right`.
  static constexpr int32_t kLabelAcceleratorGap = 16;

  // Copies the visible characters of [text, length) without '&' prefixes into
  // out (up to cap); returns the count and, through mnemonic, the index in
  // out of the underlined character (or -1).
  static size_t strip_label(const char *text, size_t length, char *out, size_t cap, int32_t &mnemonic) {
    size_t n = 0;
    mnemonic = -1;
    for (size_t i = 0; i < length && n < cap; ++i) {
      if (text[i] == '&' && i + 1 < length) {
        ++i;
        if (text[i] != '&' && mnemonic < 0) mnemonic = static_cast<int32_t>(n);
      }
      out[n++] = text[i];
    }
    return n;
  }

  static size_t label_tab(const char *text, size_t length) {
    for (size_t i = 0; i < length; ++i)
      if (text[i] == '\t') return i;
    return length;
  }

  int32_t label_part_width(const char *text, size_t length) const {
    char buf[128];
    int32_t mnemonic;
    const size_t n = strip_label(text, length, buf, sizeof buf, mnemonic);
    return width(buf, n, false);
  }

  int32_t label_width(const char *text, size_t length) const {
    const size_t tab = label_tab(text, length);
    int32_t total = label_part_width(text, tab);
    if (tab < length) total += kLabelAcceleratorGap + label_part_width(text + tab + 1, length - tab - 1);
    return total;
  }

  template <typename Fill>
  void draw_label(int32_t x, int32_t y, const char *text, size_t length, uint32_t foreground,
                  uint32_t background, bool opaque, int32_t right, Fill &&fill) const {
    const size_t tab = label_tab(text, length);
    draw_label_part(x, y, text, tab, foreground, background, opaque, fill);
    if (tab >= length) return;
    const char *accel = text + tab + 1;
    const size_t accelLength = length - tab - 1;
    draw_label_part(right - label_part_width(accel, accelLength), y, accel, accelLength, foreground,
                    background, opaque, fill);
  }

private:
  template <typename Fill>
  void draw_label_part(int32_t x, int32_t y, const char *text, size_t length, uint32_t foreground,
                       uint32_t background, bool opaque, Fill &fill) const {
    char buf[128];
    int32_t mnemonic;
    const size_t n = strip_label(text, length, buf, sizeof buf, mnemonic);
    draw(x, y, buf, n, false, foreground, background, opaque, fill);
    if (mnemonic < 0) return;
    const int32_t left = x + width(buf, static_cast<size_t>(mnemonic), false);
    const int32_t charWidth = width(buf + mnemonic, 1, false);
    const int32_t underline = y + text_above(false) + 1;
    fill(left, underline, left + charWidth, underline + 1, foreground);
  }

  UiText() = default;
  bool owns() const { return font_ == &own_; }

  static constexpr size_t kRun = 16;

  // Always fills out; returns true when vector glyphs were used.
  bool resolve(const char *text, size_t length, bool bold, font::UiGlyph *out, bool vector) const {
    return font_->ready_glyphs(text, length, bold ? 1 : 0, out, vector);
  }

  font::UiFontHandle own_;
  const font::UiFontHandle *font_{&own_};
};

} // namespace kernel::user
