#include <kernel/gfx/gfx.hpp>
#include "gfx/screen_internal.hpp"

#include <kernel/gfx.hpp>
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KGraphicsAdapter.hpp>
#include <kernel/kobject/KMemoryBackingObject.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/mutex.hpp>

namespace kernel::gfx {
namespace {
using Rect = ScreenRect;

Mutex g_sceneLock;
DisplayChangedFn g_displayChanged{nullptr};
uint32_t g_drawHold{0};

struct Cursor {
  int32_t x{0}, y{0};
  bool enabled{false}, drawn{false};
  bool adapterSaved{false};
  Rect savedRect{};
  uint8_t saved[9 * 9 * 4]{};
  uint32_t nativeSaved[9 * 9]{};
  KObjectPtr<KSurface> surface;
  KObjectPtr<KGraphicsAdapter> adapter;
} g_cursor;

// The active target, refreshed only when the topology generation moves.
// Callers hold the scene lock, which every target replacement also holds.
KObjectPtr<KDisplay> g_display;
uint32_t g_displayGeneration{0};
const KObjectPtr<KDisplay> kNoDisplay;

const KObjectPtr<KDisplay> &cursor_display() noexcept {
  const uint32_t generation = topology_generation();
  if (generation != g_displayGeneration) {
    g_display = active_video_present_target();
    g_displayGeneration = generation;
  }
  if (!g_display || !g_display->surface || !g_display->isAttachedToDesktop)
    return kNoDisplay;
  return g_display;
}

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

Rect bounds_of(const KDisplay &display) noexcept {
  return {.left = 0, .top = 0, .right = static_cast<int32_t>(display.surface->width), .bottom = static_cast<int32_t>(display.surface->height)};
}

void present(const KObjectPtr<KDisplay> &display, const Rect &r) noexcept {
  const auto &adapter = display->adapter;
  if (!adapter || !adapter->Present)
    return;
  const KClipRect rect{.left = r.left, .top = r.top, .right = r.right, .bottom = r.bottom};
  adapter->Present(display->adapter, display->surface, &rect, 1);
}

// Restores the pixels under the cursor; presents them only when asked to.
void cursor_restore(bool presentNow) noexcept {
  const auto &surface = g_cursor.surface;
  const auto *r = &g_cursor.savedRect;
  if (g_cursor.adapterSaved) {
    // One FillRect per run of equal pixels in a row, not one per pixel.
    const int32_t width = r->right - r->left;
    uint32_t index = 0;
    for (int32_t y = r->top; y < r->bottom; ++y) {
      for (int32_t x = 0; x < width;) {
        const uint32_t color = g_cursor.nativeSaved[index + static_cast<uint32_t>(x)];
        int32_t end = x + 1;
        while (end < width && g_cursor.nativeSaved[index + static_cast<uint32_t>(end)] == color) ++end;
        (void) g_cursor.adapter->FillRect(g_cursor.adapter, surface, r->left + x, y, end - x, 1, color);
        x = end;
      }
      index += static_cast<uint32_t>(width);
    }
    g_cursor.drawn = false;
    if (presentNow && g_cursor.adapter->Present) {
      const KClipRect rect{.left = r->left, .top = r->top, .right = r->right, .bottom = r->bottom};
      g_cursor.adapter->Present(g_cursor.adapter, surface, &rect, 1);
    }
    return;
  }
  auto *base = surface->backing->directAddress.as_ptr<uint8_t>();
  const auto bytes = pixel_bytes(*surface);
  uint32_t index = 0;
  for (int32_t y = r->top; y < r->bottom; ++y)
    for (int32_t x = r->left; x < r->right; ++x)
      for (uint32_t b = 0; b < bytes; ++b)
        base[static_cast<size_t>(y) * static_cast<size_t>(surface->pitch) + static_cast<size_t>(x) * bytes + b] = g_cursor.saved[index++];
  g_cursor.drawn = false;
  if (!presentNow)
    return;
  const auto &display = cursor_display();
  if (display && display->surface == surface)
    present(display, *r);
}

// Draws the cursor; `extra` (restored pixels not yet presented) joins its present.
void cursor_draw(const Rect &extra) noexcept {
  const auto &display = cursor_display();
  if (!g_cursor.enabled || g_cursor.drawn || g_drawHold || !display) {
    if (display && !extra.Empty())
      present(display, extra);
    return;
  }
  const auto &surface = display->surface;
  const auto bytes = pixel_bytes(*surface);
  const Rect screen = bounds_of(*display);
  const auto r = Rect{.left = g_cursor.x - 4, .top = g_cursor.y - 4, .right = g_cursor.x + 5, .bottom = g_cursor.y + 5}.Intersect(screen);
  const Rect damage = extra.Empty() ? r : r.Union(extra);
  if (r.Empty()) {
    if (!damage.Empty())
      present(display, damage);
    return;
  }
  g_cursor.adapterSaved = false;
  const auto &adapter = display->adapter;
  const bool packed = bytes && surface->backing && surface->backing->directAddress &&
                      surface->pitch > 0 &&
                      static_cast<uint64_t>(surface->pitch) >= static_cast<uint64_t>(surface->width) * bytes &&
                      static_cast<uint64_t>(surface->pitch) * surface->height <= surface->sizeInBytes;
  if (packed) {
    const auto *base = surface->backing->directAddress.as_ptr<uint8_t>();
    uint32_t index = 0;
    for (int32_t y = r.top; y < r.bottom; ++y)
      for (int32_t x = r.left; x < r.right; ++x)
        for (uint32_t b = 0; b < bytes; ++b)
          g_cursor.saved[index++] = base[static_cast<size_t>(y) * static_cast<size_t>(surface->pitch) + static_cast<size_t>(x) * bytes + b];
  } else {
    const auto bail = [&] {
      if (!extra.Empty())
        present(display, extra);
    };
    if (!adapter || !adapter->ReadPixel || !adapter->FillRect)
      return bail();
    uint32_t index = 0;
    for (int32_t y = r.top; y < r.bottom; ++y)
      for (int32_t x = r.left; x < r.right; ++x) {
        const auto pixel = adapter->ReadPixel(adapter, surface, x, y);
        if (!pixel)
          return bail();
        g_cursor.nativeSaved[index++] = *pixel;
      }
    g_cursor.adapterSaved = true;
  }
  g_cursor.surface = surface;
  g_cursor.adapter = adapter;
  g_cursor.savedRect = r;
  g_cursor.drawn = true;
  const Rect shapes[] = {{.left = g_cursor.x - 4, .top = g_cursor.y - 1, .right = g_cursor.x + 5, .bottom = g_cursor.y + 2},
                         {.left = g_cursor.x - 1, .top = g_cursor.y - 4, .right = g_cursor.x + 2, .bottom = g_cursor.y + 5},
                         {.left = g_cursor.x - 3, .top = g_cursor.y, .right = g_cursor.x + 4, .bottom = g_cursor.y + 1},
                         {.left = g_cursor.x, .top = g_cursor.y - 3, .right = g_cursor.x + 1, .bottom = g_cursor.y + 4}};
  for (uint32_t i = 0; i < 4; ++i) {
    const auto c = shapes[i].Intersect(screen);
    if (!c.Empty())
      (void) fill_rect_palette_index_locked(display, c.left, c.top, c.right - c.left, c.bottom - c.top, i < 2 ? 0 : 15, FillMode::Auto);
  }
  present(display, damage);
}

bool cursor_saved() noexcept {
  return g_cursor.drawn && g_cursor.surface && g_cursor.surface->backing;
}

void cursor_hide() noexcept {
  if (cursor_saved())
    cursor_restore(true);
}
}// namespace

void scene_lock() noexcept { g_sceneLock.lock(); }
void scene_unlock() noexcept { g_sceneLock.unlock(); }
bool scene_try_lock() noexcept { return g_sceneLock.try_lock(); }

void screen_draw_begin_locked() noexcept {
  cursor_hide();
  ++g_drawHold;
}

void screen_draw_end_locked() noexcept {
  if (g_drawHold)
    --g_drawHold;
  cursor_draw({});
}

ScreenRect cursor_exclude_locked(const KSurface *target, const ScreenRect &bounds) noexcept {
  if (!cursor_saved() || g_cursor.surface.get() != target || g_cursor.savedRect.Intersect(bounds).Empty())
    return {};
  const Rect restored = g_cursor.savedRect;
  cursor_restore(false);
  return restored;
}

void cursor_include_locked(const ScreenRect &restored) noexcept {
  if (restored.Empty())
    return;
  cursor_draw(restored);
}

void cursor_reset_locked() noexcept {
  g_display = nullptr;
  g_displayGeneration = 0;
  g_cursor.drawn = false;
  g_cursor.adapterSaved = false;
  g_cursor.surface = nullptr;
  g_cursor.adapter = nullptr;
  g_cursor.savedRect = {};
}

void SetCursorPosition(int32_t x, int32_t y) noexcept {
  SceneGuard scene;
  // One present covers both the old and the new cursor position.
  Rect restored{};
  if (cursor_saved()) {
    restored = g_cursor.savedRect;
    cursor_restore(false);
  }
  g_cursor.x = x;
  g_cursor.y = y;
  g_cursor.enabled = true;
  cursor_draw(restored);
}

void BeginScreenDraw() noexcept {
  SceneGuard scene;
  screen_draw_begin_locked();
}

void EndScreenDraw() noexcept {
  SceneGuard scene;
  screen_draw_end_locked();
}

void PresentDisplay(const KObjectPtr<KDisplay> &display, kstd::span<const ScreenRect> rects) noexcept {
  SceneGuard scene;
  if (rects.empty() || !display || cursor_display().get() != display.get())
    return;
  const auto &adapter = display->adapter;
  uint32_t count = 0;
  for ([[maybe_unused]] const ScreenRect &rect: rects) ++count;
  if (adapter && adapter->Present)
    adapter->Present(display->adapter, display->surface, reinterpret_cast<const KClipRect *>(rects.data()), count);
  // Outside a screen draw the presented pixels are final: put the cursor back.
  cursor_draw({});
}

bool FillRectPaletteIndex(const KObjectPtr<KDisplay> &display, int32_t x, int32_t y, int32_t w,
                          int32_t h, uint32_t index, FillMode mode) noexcept {
  SceneGuard scene;
  if (!display || cursor_display().get() != display.get())
    return false;
  if (g_drawHold || mode == FillMode::Deferred)
    return fill_rect_palette_index_locked(display, x, y, w, h, index, mode);
  // Keep the cursor's saved pixels valid: lift it off the filled area. It
  // comes back with the caller's PresentDisplay, or now if nothing is pending.
  (void) cursor_exclude_locked(display->surface.get(), Rect{.left = x, .top = y, .right = x + w, .bottom = y + h});
  const bool pending = fill_rect_palette_index_locked(display, x, y, w, h, index, mode);
  if (!pending)
    cursor_draw({});
  return pending;
}

void RegisterDisplayChangedHandler(DisplayChangedFn handler) noexcept { g_displayChanged = handler; }

void notify_display_changed(const DisplayChange &change) noexcept {
  if (const auto handler = g_displayChanged)
    handler(change);
}

}// namespace kernel::gfx
