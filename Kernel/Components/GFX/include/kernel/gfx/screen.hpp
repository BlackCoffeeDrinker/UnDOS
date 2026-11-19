#pragma once

#include <stdint.h>

#include <expected.hpp>
#include <span.hpp>

#include <kernel/kerror.hpp>
#include <kernel/fwd/KDisplay.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/gfx/clip_rect.hpp>
#include <kernel/gfx/dc_fwd.hpp>
#include <kernel/gfx/region.hpp>

namespace kernel {
struct KProcessObject;
struct KSurface;
}// namespace kernel

// Screen ownership: the software cursor overlay and the display-change
// handler a desktop subsystem registers. GFX knows nothing about windows; it
// only reports display changes. Every function here serializes
// itself on the GFX scene lock, which is private to GFX; callers may hold
// their own locks, which always come before it.
namespace kernel::gfx {

using ScreenRect = user::URect;

// Software cursor on the active display.
void SetCursorPosition(int32_t x, int32_t y) noexcept;
// A caller is drawing to the screen: the cursor stays off the screen until
// the matching end. Nests and may span syscalls; no lock stays held.
void BeginScreenDraw() noexcept;
void EndScreenDraw() noexcept;

// Presents `rects` of `display` when it is still the active display.
void PresentDisplay(const KObjectPtr<KDisplay> &display, kstd::span<const ScreenRect> rects) noexcept;

struct DisplayChange {
  // The active display after the change; null when none is active.
  KObjectPtr<KDisplay> active;
  uint32_t bpp{0};
  // A mode was committed: input coordinates must be refitted.
  bool modeCommitted{false};
  bool firstActivation{false};
};

// Called without any GFX lock held, after the change is published.
using DisplayChangedFn = void (*)(const DisplayChange &change) noexcept;

// Registered once by the desktop subsystem at init. Null unregisters.
void RegisterDisplayChangedHandler(DisplayChangedFn handler) noexcept;

// Window device contexts for the desktop subsystem. Each takes the GFX scene
// lock.
kstd::expected<HDC, kstd::error_code> CreateDC(KProcessObject *process);
bool DCExists(KProcessObject *process, HDC hdc);
// Moves the slot's owners into `released`; drop it after the caller's locks.
kstd::error_code CloseDC(KProcessObject *process, HDC hdc, DetachedDC &released);
// Drops the owners inline: call without the USER lock.
kstd::error_code CloseDC(KProcessObject *process, HDC hdc);

// Window binding of a DC. `clip` is copied into `clipStorage` under the scene
// lock; the DC then reads `clipStorage`, which must stay valid until the DC
// is closed or detached, and may be written by its owner only through these
// calls. `clipStorage` must hold at least `clip.size()` rectangles.
struct WindowDCBinding {
  // Non-null: draw to this surface without acceleration.
  KSurface *target{nullptr};
  int32_t originX{0}, originY{0};
  kstd::span<KClipRect> clipStorage;
  kstd::span<const KClipRect> clip;
  uint32_t hwnd{0};
};
kstd::error_code BindWindowDC(KProcessObject *process, HDC hdc, const WindowDCBinding &binding);
// The DC keeps existing but draws nothing and stops reading its clip storage.
void DetachWindowDC(KProcessObject *process, HDC hdc);
// Closes the DC and returns the screen damage its draws left (empty if none).
// `incomplete` is set when a draw through it failed part way; the window
// should repaint.
kstd::error_code CloseWindowDC(KProcessObject *process, HDC hdc, KClipRect &damage, bool &incomplete,
                               DetachedDC &released);
// Forgets damage already superseded by a full repaint.
void ResetDCPresentation(KProcessObject *process, HDC hdc);

// Registry handle of the font face resource `face`; 0 when it is not one.
uint32_t FontRegistryHandle(uint32_t face);

}// namespace kernel::gfx
