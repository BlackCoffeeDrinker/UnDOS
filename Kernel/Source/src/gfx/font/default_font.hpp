#pragma once

#include "realization.hpp"

namespace kernel::font {

// The session's UI default: regular and bold realizations at one ppem. A
// snapshot holds references, so it stays valid and self-consistent after the
// configuration is replaced or the publishing process exits.
// Pinned masks for the printable ASCII range and Terminal replacement glyph
// of both faces, built while the configuration is prepared. USER chrome and
// default-font text calls share these masks, so a generation has one stable
// raster for its measurements and drawing.
struct UiGlyphs final : RefCounted {
  static constexpr uint32_t FirstCodepoint = 0x20;
  static constexpr uint32_t Count = 0x7f - FirstCodepoint;
  static constexpr int32_t MenuVerticalPadding = 2;
  // Index 0 is the regular face, 1 is the bold face.
  Ref<GlyphMask> masks[2][Count];
  Ref<GlyphMask> terminalReplacement[2];
  uint32_t ascent[2]{};
  uint32_t descent[2]{};
  uint32_t lineGap[2]{};
  int32_t textAbove[2]{};
  int32_t textBelow[2]{};
  int32_t textHeight[2]{};
};

struct UiFont {
  Ref<Realization> regular;
  Ref<Realization> bold;
  Ref<UiGlyphs> ascii;
  uint32_t ppem{0};
  bool bitmapBackend{false};
  // Starts at 1 and grows with every publication. USER and SDK controls compare
  // it with the generation of their cached measurements.
  uint32_t generation{0};
  explicit operator bool() const {
    return bitmapBackend || (static_cast<bool>(regular) && static_cast<bool>(bold));
  }
};

// Holds the shared UI default font, separate from any DC or application font
// selection. Any process may publish; the last successful publication wins.
// Before the first publication snapshot() is empty and callers use the bitmap
// recovery path.
class DefaultFont {
public:
  using PublishCallback = kstd::error_code (*)(void *, DefaultFont &, UiFont &&);

  // The registry must outlive this object.
  explicit DefaultFont(FaceRegistry &faces) : faces_(faces) {}

  // Resolves both face handles, then prepares both realizations (slow: runs
  // font and prep programs) before replacing the configuration in one step.
  // Any failure leaves the previous configuration unchanged. Errors:
  // InvalidHandle, or the error from Realization::create (including
  // InvalidArgument for a ppem outside MinPpem..MaxPpem).
  kstd::error_code publish(uint32_t regularHandle, uint32_t boldHandle, uint32_t ppem);
  // Holds publisher serialization through preparation and callback. The
  // callback may commit the candidate while holding the USER scene lock.
  kstd::error_code publish(uint32_t regularHandle, uint32_t boldHandle, uint32_t ppem,
                           PublishCallback callback, void *context);
  kstd::error_code publish_builtin(PublishCallback callback, void *context);

  UiFont snapshot() const;
  // The caller holds publisher serialization and performs any coupled USER
  // state update under the USER lock. Displaced references are returned so
  // they can be released after leaving that lock.
  void commit(UiFont &&candidate, UiFont &displaced);

private:
  kstd::expected<UiFont, kstd::error_code> prepare(uint32_t regularHandle, uint32_t boldHandle,
                                                   uint32_t ppem);
  FaceRegistry &faces_;
  mutable Mutex mutex_;
  UiFont current_;
  // Serializes publishers so slow preparation never runs under mutex_.
  Mutex publishMutex_;
};

} // namespace kernel::font
