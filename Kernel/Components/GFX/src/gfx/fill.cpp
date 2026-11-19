#include <kernel/gfx/fill.hpp>

#include "gfx/screen_internal.hpp"

#include "gfx/palette16.hpp"

#include <kernel/gfx.hpp>
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KGraphicsAdapter.hpp>
#include <kernel/kobject/KMemoryBackingObject.hpp>
#include <kernel/kobject/KSurface.hpp>

namespace kernel::gfx {
namespace {

uint32_t pixel_bytes(const KSurface &surface) noexcept {
  switch (surface.pixelFormat) {
    case PixelFormat::INDEX8: return 1;
    case PixelFormat::RGB15:
    case PixelFormat::RGB16: return 2;
    case PixelFormat::RGB24: return 3;
    case PixelFormat::ARGB32: return 4;
  }
  return 0;
}

// The surface has a CPU-visible linear buffer the software path can write.
bool software_writable(const KSurface &surface) noexcept {
  return surface.backing && surface.backing->directAddress && surface.pitch > 0 &&
         static_cast<uint64_t>(surface.pitch) >=
             static_cast<uint64_t>(surface.width) * pixel_bytes(surface);
}

constexpr uint64_t kSmallFillArea = 256;

} // namespace

bool fill_rect_palette_index_locked(const KObjectPtr<KDisplay> &display, int32_t x, int32_t y, int32_t w,
                                    int32_t h, uint32_t index, FillMode mode) noexcept {
  if (!display || !display->surface || w <= 0 || h <= 0) return false;
  const auto &surface = display->surface;
  const uint32_t packed = PaletteScreenColor(surface->palette, index, surface->bpp);
  if (mode == FillMode::ShadowOnly)
    return !KE_GFX_SoftwareFillRect(surface, x, y, w, h, packed);

  const uint64_t area = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
  if ((area <= kSmallFillArea || mode == FillMode::Deferred) && software_writable(*surface) &&
      !KE_GFX_SoftwareFillRect(surface, x, y, w, h, packed))
    return true;
  const auto &adapter = display->adapter;
  if (adapter && adapter->FillRect && !adapter->FillRect(adapter, surface, x, y, w, h, packed))
    return !Any(adapter->caps & KGfxCaps::VisibleFill);
  // Last resort; scene painting has no caller to report a failed fill to.
  (void) KE_GFX_SoftwareFillRect(surface, x, y, w, h, packed);
  return true;
}

} // namespace kernel::gfx
