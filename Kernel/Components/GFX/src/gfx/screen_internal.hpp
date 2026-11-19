#pragma once

#include <kernel/gfx/fill.hpp>
#include <kernel/gfx/screen.hpp>
#include "gfx/KDeviceContextTable.hpp"

// GFX-private screen ownership: the scene lock and the cursor primitives that
// run under it. Lock order: USER window lock -> scene lock -> per-DC, driver
// or topology lock. The scene lock is not recursive; public GFX entry points
// take it, so code holding it calls only the *_locked forms below. No desktop
// hook is ever called with it held.
namespace kernel::gfx {

void scene_lock() noexcept;
void scene_unlock() noexcept;
bool scene_try_lock() noexcept;

struct SceneGuard {
  SceneGuard() noexcept { scene_lock(); }
  ~SceneGuard() { scene_unlock(); }
  SceneGuard(const SceneGuard &) = delete;
  SceneGuard &operator=(const SceneGuard &) = delete;
};

// The cursor stays off the screen until the matching end. Nests.
void screen_draw_begin_locked() noexcept;
void screen_draw_end_locked() noexcept;

struct ScreenDrawHold {
  ScreenDrawHold() noexcept { screen_draw_begin_locked(); }
  ~ScreenDrawHold() { screen_draw_end_locked(); }
  ScreenDrawHold(const ScreenDrawHold &) = delete;
  ScreenDrawHold &operator=(const ScreenDrawHold &) = delete;
};

// Hides the cursor without presenting, only when it lies on `target` inside
// `bounds`. Returns the restored rectangle (empty if untouched); pass it to
// cursor_include_locked after drawing so one present covers both.
ScreenRect cursor_exclude_locked(const KSurface *target, const ScreenRect &bounds) noexcept;
void cursor_include_locked(const ScreenRect &restored) noexcept;
// Drops the saved pixels; the surface under them is gone.
void cursor_reset_locked() noexcept;

bool fill_rect_palette_index_locked(const KObjectPtr<KDisplay> &display, int32_t x, int32_t y, int32_t w,
                                    int32_t h, uint32_t index, FillMode mode) noexcept;

// Delivers a display change to the registered handler. Callers hold no GFX lock.
void notify_display_changed(const DisplayChange &change) noexcept;

// DC table access under the scene lock.
kstd::expected<kstd::pair<KDeviceContextSlot *, HDC>, kstd::error_code> create_dc_locked(KProcessObject *process);
KDeviceContextSlot *find_dc_locked(KProcessObject *process, HDC dc);
kstd::error_code close_dc_locked(KProcessObject *process, HDC dc, DetachedDC &released);

}// namespace kernel::gfx
