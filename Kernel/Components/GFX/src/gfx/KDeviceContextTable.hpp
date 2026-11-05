#pragma once

#include <__memory/unique_ptr.hpp>
#include <__utility/pair.hpp>
#include <array.hpp>
#include <expected.hpp>
#include <list.hpp>
#include <vector.hpp>

#include <kernel/__core.hpp>
#include <kernel/gfx/clip_rect.hpp>
#include <kernel/fwd/KDisplay.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kerror.hpp>
#include <kernel/mutex.hpp>

namespace kernel {
struct KObject;
struct KSurface;

// GDI-style device context handle: bits 0-15 are the slot index, bits 16-31
// are the slot's generation tag (bumped on every Close(), rejecting stale
// handles that alias a reused index).
using HDC = uint32_t;
constexpr HDC INVALID_HDC = 0;

namespace gfx {
// Process that created a DC; used only by the exit sweep (no access check).
struct ProcessKey {
  uint32_t pid{0};
  uint64_t incarnation{0};
  bool operator==(const ProcessKey &) const = default;
};
// PID 0: system/un-owned DCs, never swept.
constexpr ProcessKey kSystemOwner{.pid = 0, .incarnation = 0};
}// namespace gfx

// Ephemeral device-context state: target of drawing primitives
// (Rectangle, LineTo, BitBlt, ...). Unlike KDisplay/KSurface (long-lived
// resources managed by the Object Manager), DC slots live only inside a
// the GFX DC table and are never KObjects themselves.
struct KDeviceContextSlot {
  bool used = false;
  // Bumped every Close() to invalidate stale HDCs. Starts at 1 (not 0) so
  // the very first handle ever allocated for a given slot (index 0,
  // generation 1) never computes to INVALID_HDC (0).
  uint16_t generation = 1;
  uint64_t renderGeneration = 1;
  bool renderRetired = false;
  uint32_t nextFree = UINT32_MAX;
  gfx::ProcessKey owner{};// set by Allocate, cleared on reclaim
  // Call with the scene lock held before changing render state.
  bool ChangeRenderState() noexcept {
    if (renderRetired) return false;
    if (renderGeneration == UINT64_MAX) {
      renderRetired = true;
      return false;
    }
    ++renderGeneration;
    return true;
  }
  // Slot fields are guarded by the USER scene lock (every GDI path holds it).

  KObjectPtr<KSurface> target;       // selected surface; its display (if any) gives adapter and screen
  KObjectPtr<KObject> selectedFont;  // immutable font snapshot selected for text drawing

  uint32_t penColor = 0;
  uint32_t backgroundColor = 7;
  bool opaqueText = false;
  bool memoryDC = false;
  bool noAccel = false;
  // Presentation damage is independent of the live USER occlusion clip, which
  // can change while a paint DC is active.
  bool presentationDirty = false;
  KClipRect presentationBounds{};

  // MoveTo/LineTo cursor
  int32_t currentX = 0;
  int32_t currentY = 0;

  // Added to every coordinate before drawing (window DCs from BeginPaint).
  int32_t originX = 0;
  int32_t originY = 0;
  // When clipped, drawing is limited to clipRects[0..clipCount). The rects
  // live in window `hwnd`'s paintClip; USER closes the DC before retiring the
  // window and bumps renderGeneration on every rebind (the clip generation).
  bool clipped = false;
  const KClipRect *clipRects = nullptr;
  uint32_t clipCount = 0;
  uint32_t hwnd = 0;// BeginPaint window, 0 if none (non-owning)
};

// Maximum number of live DCs system-wide. Fixed so GDI calls never allocate.
constexpr size_t kMaxDeviceContexts = 4096;
static_assert(kMaxDeviceContexts <= UINT16_MAX + 1, "HDC index is 16 bits");

// Owners moved out of a closed slot. Drop it after the USER scene lock is
// released: the last release can free heap memory and wait on the arena.
struct DetachedDC {
  KObjectPtr<KSurface> target;
  KObjectPtr<KObject> font;
};

// GFX-internal DC table (file-static in gfx_dc_table.cpp). Lock order: USER
// scene lock -> GFX lock. The GFX lock never masks interrupts.
namespace gfx {
// ResourceLimitExceeded when all kMaxDeviceContexts are in use.
kstd::expected<kstd::pair<KDeviceContextSlot *, HDC>, kstd::error_code> AllocateDC(ProcessKey owner) noexcept;
// O(1): rejects an out-of-range index, an unused slot or a stale generation.
KDeviceContextSlot *ResolveDC(HDC hdc) noexcept;
// Invalidates `hdc` and moves the slot's owners into `released`.
kstd::error_code CloseDCSlot(HDC hdc, DetachedDC &released) noexcept;
// Exit sweep only: closes every slot owned by `owner` (never kSystemOwner),
// dropping owners with no lock held. Call without the USER scene lock.
size_t CloseAllOwnedBy(ProcessKey owner) noexcept;
}// namespace gfx

}// namespace kernel
