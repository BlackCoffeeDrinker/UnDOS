#pragma once

#include <stdint.h>

#include <kernel/fwd/KDisplay.hpp>
#include <kernel/fwd/KObjectPtr.hpp>

namespace kernel::gfx {

enum class FillMode : uint8_t {
  // Small or deferred fills go to the packed shadow; others try the adapter
  // first and fall back to software.
  Auto,
  // Inside a scene draw: keep pixels in the shadow so the frame appears
  // atomically at the final present.
  Deferred,
  // Shadow only, never the adapter.
  ShadowOnly,
};

// Fills [x, x + w) x [y, y + h) of the display surface with palette entry
// `index` of that surface's palette. Returns true when the pixels still have
// to be presented (written to the surface, not necessarily to the screen).
// Takes the GFX scene lock; does nothing unless `display` is the active display.
bool FillRectPaletteIndex(const KObjectPtr<KDisplay> &display, int32_t x, int32_t y, int32_t w,
                          int32_t h, uint32_t index, FillMode mode = FillMode::Auto) noexcept;

} // namespace kernel::gfx
