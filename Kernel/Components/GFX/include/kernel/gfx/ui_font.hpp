#pragma once

#include <stddef.h>
#include <stdint.h>
#include <system_error.hpp>

namespace kernel::font {

class DefaultFont;
struct UiFont;

// Printable ASCII range covered by UI fonts, and the menu row padding.
inline constexpr uint32_t kUiFirstCodepoint = 0x20;
inline constexpr uint32_t kUiCodepointCount = 0x7f - kUiFirstCodepoint;
inline constexpr int32_t kUiMenuVerticalPadding = 2;

// A ready glyph mask: 1 bit per pixel, MSB first, `stride` bytes per row.
// left/top are pixels from the pen origin with y growing upward. `bits` stays
// valid while the UiFontHandle that produced it is alive.
struct UiGlyph {
  int32_t left;
  int32_t top;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  int32_t advance;
  const uint8_t *bits;
};

// Opaque, copyable reference to a published UI font (regular and bold). It
// never allocates: copies only take references.
class UiFontHandle {
public:
  UiFontHandle() noexcept;
  UiFontHandle(const UiFontHandle &other) noexcept;
  UiFontHandle(UiFontHandle &&other) noexcept;
  UiFontHandle &operator=(const UiFontHandle &other) noexcept;
  UiFontHandle &operator=(UiFontHandle &&other) noexcept;
  ~UiFontHandle();

  // Takes over a font handed out by DefaultFont (publish callbacks).
  static UiFontHandle adopt(UiFont &&font) noexcept;
  // Copy of the session default, or an empty handle before the font system
  // exists. Never allocates, so it is safe under the scene lock.
  static UiFontHandle snapshot_default() noexcept;

  explicit operator bool() const noexcept;
  // True when ASCII glyph tables exist; otherwise only bitmap glyphs are used.
  bool has_glyphs() const noexcept;
  uint32_t generation() const noexcept;
  // style: 0 regular, 1 bold. Line metrics cover the built-in bitmap font too.
  int32_t text_height(uint32_t style) const noexcept;
  int32_t text_above(uint32_t style) const noexcept;

  // Call before scene-locked drawing.
  void prewarm(const char *text, size_t length, uint32_t style) const noexcept;
  bool all_ready(const char *text, size_t length, uint32_t style) const noexcept;
  // Fills out[i] for each char. Uses the vector glyphs when preferVector is set
  // and every glyph is ready; otherwise the whole run gets built-in bitmap
  // glyphs, positioned in the same line box. Returns true for vector glyphs.
  bool ready_glyphs(const char *text, size_t length, uint32_t style, UiGlyph *out,
                    bool preferVector = true) const noexcept;

  // GFX-internal access; UiFont is complete only inside GFX.
  UiFont &get() noexcept;
  const UiFont &get() const noexcept;

  static constexpr size_t kStorageSize = 8 * sizeof(void *);

private:
  alignas(void *) unsigned char storage_[kStorageSize];
};

using UiPublishCallback = kstd::error_code (*)(void *, DefaultFont &, UiFont &&);

// The system default font, creating the font system on first use; null when
// it cannot be created.
DefaultFont *system_default_font() noexcept;

// Wrappers over DefaultFont for components that only see this header.
kstd::error_code publish_default(DefaultFont &defaults, uint32_t regularHandle, uint32_t boldHandle,
                                 uint32_t ppem, UiPublishCallback callback, void *context) noexcept;
kstd::error_code publish_builtin_default(DefaultFont &defaults, UiPublishCallback callback,
                                         void *context) noexcept;
// Caller holds publisher serialization (inside a publish callback).
void commit_default(DefaultFont &defaults, UiFontHandle &&candidate, UiFontHandle &displaced) noexcept;

} // namespace kernel::font
