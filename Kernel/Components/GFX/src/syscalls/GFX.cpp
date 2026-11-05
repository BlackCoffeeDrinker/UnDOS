#include "gfx/gfx.hpp"
#include "gfx/adapter_ops.hpp"
#include "gfx/bitmap_font.hpp"
#include "gfx/detail/raster_helpers.hpp"
#include "gfx/font/FontObject.hpp"
#include "gfx/font/font_system.hpp"
#include "gfx/font/text.hpp"
#include "gfx/process_state.hpp"

#include "stdkrn.hpp"
#include "syscalls.hpp"
#include "user/user.hpp"

#include <expected.hpp>

#include <kernel/kobject/KVideoPresentValidationToken.hpp>
#include <undos/font/resource.hpp>

// GFX (GDI-style) syscall handlers (300-399), generated from
// tools/gfxsyscall.txt (pulled into tools/syscalls.txt via `include`).
//
// Palette16 drawing state, fixed bitmap text and SRCCOPY are the minimal shell
// contract. Unused object/region/stretch/alpha APIs remain explicitly unsupported.
//
// Device contexts (DCs) are ephemeral per-process state, not Object-Manager
// resources: all DCs live in one fixed GFX-private table behind gfx::CreateDC /
// FindDC / CloseDC, resolved via a generation-tagged HDC handle rather than
// the generic ProcessHandleTable used for long-lived KObjects like KSurface.

#if __STDC_HOSTED__
// Hosted tests only: runs after DrawText retains its font and drops all locks.
namespace kernel::gfx::test_hooks {
void (*drawTextAfterRetain)() = nullptr;
void (*textInterruptible)() = nullptr;
void (*textSlice)(uint32_t, uint32_t) = nullptr;
}// namespace kernel::gfx::test_hooks
#endif

namespace {

struct TextEntryState {
  uintptr_t state = HAL_CPU_SaveAndDisableInterrupts();
  ~TextEntryState() {
    HAL_CPU_DisableInterrupts();
    HAL_CPU_RestoreInterrupts(state);
  }
};

struct TextInterruptWindow {
  TextInterruptWindow() {
    HAL_CPU_EnableInterrupts();
#if __STDC_HOSTED__
    if (kernel::gfx::test_hooks::textInterruptible)
      kernel::gfx::test_hooks::textInterruptible();
#endif
  }
  ~TextInterruptWindow() {
    HAL_CPU_DisableInterrupts();
  }
};

kstd::error_code ValidateText(uint32_t style, uint32_t layout, uint32_t repertoire,
                              const char *text, size_t size) {
  if (style > TextStyle_StyleBold || layout > TextLayoutMode_LayoutUniformGrid ||
      repertoire > TextRepertoire_RepertoireTerminal)
    return kernel::KernelError::InvalidArgument;
  if (size > kernel::font::MaxTextBytes) return kernel::KernelError::InvalidSize;
  if (size && !text) return kernel::KernelError::InvalidArgument;
  return {};
}

kernel::font::ResourceBudget fontResourceBudget;
kernel::font::FontSystem *fontSystem() { return kernel::font::font_system(); }

kernel::KObjectPtr<kernel::font::FontObject> ResolveFont(kernel::KProcessObject &process,
                                                         uint32_t handle) {
  const auto *entry = process.handles.Lookup(static_cast<kernel::KHandle>(handle));
  return entry ? entry->object.As<kernel::font::FontObject>()
               : kernel::KObjectPtr<kernel::font::FontObject>{};
}

const kernel::font::Ref<kernel::font::Realization> &RealizationFor(
    const kernel::font::FontObject &font, uint32_t style) {
  return style ? font.snapshot.bold : font.snapshot.regular;
}

kstd::expected<kernel::font::Ref<kernel::font::GlyphMask>, kstd::error_code> PinnedFontMask(
    const kernel::font::FontObject &font, uint32_t style, uint32_t codepoint) {
  if (!font.snapshot.ascii) return kernel::font::Ref<kernel::font::GlyphMask>{};
  if (codepoint == 0xfffd ||
      (codepoint >= kernel::font::UiGlyphs::FirstCodepoint &&
       codepoint < kernel::font::UiGlyphs::FirstCodepoint + kernel::font::UiGlyphs::Count))
    return font.snapshot.ascii->glyph(style, codepoint);
  return kernel::font::Ref<kernel::font::GlyphMask>{};
}

void BitmapMetrics(uint32_t generation, uint32_t repertoire, TextMetricsV2 &metrics) {
  metrics = {};
  int32_t left = INT32_MAX, top = INT32_MAX, right = INT32_MIN, bottom = INT32_MIN;
  auto include = [&](uint32_t codepoint) {
    const uint8_t character = codepoint >= kernel::gfx::kFontFirstChar &&
                                      codepoint <= kernel::gfx::kFontLastChar
                                  ? static_cast<uint8_t>(codepoint)
                                  : static_cast<uint8_t>(kernel::gfx::kFontDefaultChar);
    for (uint32_t row = 0; row < 7; ++row) {
      const uint8_t bits = kernel::gfx::BitmapGlyphRow(character, row);
      for (uint32_t column = 0; column < 5; ++column) {
        if (!(bits & (16u >> column))) continue;
        const int32_t x = static_cast<int32_t>(column + 1);
        const int32_t y = static_cast<int32_t>(row * 2);
        if (x < left) left = x;
        if (x + 1 > right) right = x + 1;
        if (y < top) top = y;
        if (y + 2 > bottom) bottom = y + 2;
      }
    }
  };
  for (uint32_t codepoint = kernel::gfx::kFontFirstChar;
       codepoint <= kernel::gfx::kFontLastChar; ++codepoint)
    include(codepoint);
  if (repertoire == TextRepertoire_RepertoireTerminal) include(0xfffd);
  if (left == INT32_MAX) left = top = right = bottom = 0;
  metrics.generation = generation;
  metrics.ascent = kernel::gfx::kFontAscent;
  metrics.descent = kernel::gfx::kFontDescent;
  metrics.lineHeight = kernel::gfx::kFontCellHeight;
  metrics.baselineOffset = kernel::gfx::kFontAscent;
  metrics.cellWidth = kernel::gfx::kFontCellWidth;
  metrics.cellHeight = kernel::gfx::kFontCellHeight;
  metrics.inkLeft = left;
  metrics.inkTop = top;
  metrics.inkRight = right;
  metrics.inkBottom = bottom;
}

bool is_control_codepoint(uint32_t codepoint) {
  return codepoint < 0x20 || codepoint == 0x7f;
}

bool use_bitmap_fallback(const kernel::font::FontObject &font, uint32_t codepoint,
                         uint32_t glyphId) {
  return font.fallback == GlyphFallbackPolicy_FallbackBitmapForUnmapped &&
         !is_control_codepoint(codepoint) && glyphId == 0;
}

kstd::error_code FontMetrics(const kernel::font::FontObject &font, uint32_t style,
                             uint32_t repertoire, TextMetricsV2 &metrics) {
  if (style > 1) return kernel::KernelError::InvalidArgument;
  if (repertoire != TextRepertoire_RepertoireBasic &&
      repertoire != TextRepertoire_RepertoireTerminal)
    return kernel::KernelError::InvalidArgument;
  if (font.bitmap) {
    BitmapMetrics(font.generation, repertoire, metrics);
    return {};
  }
  if (!RealizationFor(font, style) || !font.snapshot.ascii)
    return kernel::KernelError::InvalidState;
  const auto &summary = font.snapshot.ascii->summaries[style]
                                                      [repertoire == TextRepertoire_RepertoireTerminal ? 1 : 0]
                                                      [font.fallback == GlyphFallbackPolicy_FallbackBitmapForUnmapped ? 1 : 0];
  metrics = {};
  metrics.generation = font.generation;
  metrics.ascent = summary.ascent;
  metrics.descent = summary.descent;
  metrics.lineGap = summary.lineGap;
  metrics.lineHeight = summary.lineHeight;
  metrics.baselineOffset = summary.baselineOffset;
  metrics.cellWidth = summary.cellWidth;
  metrics.cellHeight = summary.cellHeight;
  metrics.inkLeft = summary.inkLeft;
  metrics.inkTop = summary.inkTop;
  metrics.inkRight = summary.inkRight;
  metrics.inkBottom = summary.inkBottom;
  return {};
}

kstd::error_code LayoutFontText(const kernel::font::FontObject &font, uint32_t style,
                                const char *text, size_t size,
                                kernel::font::TextRun &run) {
  run.glyphs = kstd::vector<kernel::font::Ref<kernel::font::GlyphMask>>();
  run.glyphIds = kstd::vector<uint32_t>();
  run.width = 0;
  if (size > kernel::font::MaxTextBytes) return kernel::KernelError::InvalidSize;
  if (size && !text) return kernel::KernelError::InvalidArgument;
  const auto &realization = RealizationFor(font, style);
  if (!realization || !font.snapshot.ascii)
    return kernel::KernelError::InvalidState;
  const auto *bytes = reinterpret_cast<const uint8_t *>(text);
  kernel::font::Ref<kernel::font::GlyphMask> fallbackMask;
  uint32_t fallbackCodepoint = UINT32_MAX;
  for (size_t at = 0; at < size;) {
    const auto step = kernel::font::decode_utf8(bytes + at, size - at);
    at += step.length;
    const uint32_t glyphId = is_control_codepoint(step.codepoint)
                                 ? 0
                                 : realization->face()->glyphFor(step.codepoint);
    bool bitmapFallback = use_bitmap_fallback(font, step.codepoint, glyphId);
    kernel::font::Ref<kernel::font::GlyphMask> glyph;
    if (bitmapFallback) {
      const uint32_t bitmapCodepoint =
          step.codepoint >= kernel::gfx::kFontFirstChar &&
                  step.codepoint <= kernel::gfx::kFontLastChar
              ? step.codepoint
              : kernel::gfx::kFontDefaultChar;
      if (!fallbackMask || fallbackCodepoint != bitmapCodepoint) {
        auto created = kernel::font::builtin_bitmap_glyph(bitmapCodepoint);
        if (!created) return created.error();
        fallbackMask = *created;
        fallbackCodepoint = bitmapCodepoint;
      }
      glyph = fallbackMask;
    } else {
      size_t found = 0;
      while (found < run.glyphIds.size() && run.glyphIds[found] != glyphId) ++found;
      if (found < run.glyphIds.size()) {
        glyph = run.glyphs[found];
      } else {
        auto pinned = PinnedFontMask(font, style, step.codepoint);
        if (!pinned) return pinned.error();
        glyph = *pinned;
        if (!glyph) {
          auto rendered = realization->glyph(glyphId);
          if (!rendered) return rendered.error();
          glyph = *rendered;
        }
      }
    }
    const size_t before = run.glyphs.size();
    run.glyphs.push_back(glyph);
    if (run.glyphs.size() != before + 1)
      return kernel::KernelError::OutOfMemory;
    run.glyphIds.push_back(bitmapFallback ? UINT32_MAX : glyphId);
    if (run.glyphIds.size() != run.glyphs.size())
      return kernel::KernelError::OutOfMemory;
    if (glyph->advance > INT32_MAX - run.width) return kernel::KernelError::OutOfRange;
    run.width += glyph->advance;
  }
  return {};
}

kstd::error_code PrepareFontText(const kernel::font::FontObject &font, uint32_t style,
                                 uint32_t layout, uint32_t repertoire,
                                 const char *text, size_t size,
                                 kernel::font::TextRun &run, TextMetricsV2 &metrics,
                                 TextExtentV2 &extent) {
  extent = {};
  if (size > kernel::font::MaxTextBytes) return kernel::KernelError::InvalidSize;
  if (size && !text) return kernel::KernelError::InvalidArgument;
  if (layout != TextLayoutMode_LayoutProportional &&
      layout != TextLayoutMode_LayoutUniformGrid)
    return kernel::KernelError::InvalidArgument;
  if (const auto error = FontMetrics(font, style, repertoire, metrics); error) return error;
  if (font.bitmap) {
    uint64_t pen = 0;
    int64_t left = INT64_MAX, right = INT64_MIN, top = INT64_MAX, bottom = INT64_MIN;
    const auto *bytes = reinterpret_cast<const uint8_t *>(text);
    for (size_t at = 0; at < size;) {
      const auto step = kernel::font::decode_utf8(bytes + at, size - at);
      at += step.length;
      const uint8_t character = !is_control_codepoint(step.codepoint) &&
                                        step.codepoint >= kernel::gfx::kFontFirstChar &&
                                        step.codepoint <= kernel::gfx::kFontLastChar
                                    ? static_cast<uint8_t>(step.codepoint)
                                    : static_cast<uint8_t>(kernel::gfx::kFontDefaultChar);
      for (uint32_t row = 0; row < 7; ++row) {
        const uint8_t bits = kernel::gfx::BitmapGlyphRow(character, row);
        for (uint32_t column = 0; column < 5; ++column) {
          if (!(bits & (16u >> column))) continue;
          const int64_t inkLeft = static_cast<int64_t>(pen) + column + 1;
          const int64_t inkTop = row * 2;
          if (inkLeft < left) left = inkLeft;
          if (inkLeft + 1 > right) right = inkLeft + 1;
          if (inkTop < top) top = inkTop;
          if (inkTop + 2 > bottom) bottom = inkTop + 2;
        }
      }
      pen += metrics.cellWidth;
    }
    const uint64_t advance = pen;
    if (advance > UINT32_MAX) return kernel::KernelError::OutOfRange;
    if (left == INT64_MAX) left = right = top = bottom = 0;
    extent.generation = font.generation;
    extent.advanceWidth = static_cast<uint32_t>(advance);
    extent.lineHeight = metrics.lineHeight;
    extent.baselineOffset = metrics.baselineOffset;
    if (right > INT32_MAX || bottom > INT32_MAX) return kernel::KernelError::OutOfRange;
    extent.inkLeft = static_cast<int32_t>(left);
    extent.inkTop = static_cast<int32_t>(top);
    extent.inkRight = static_cast<int32_t>(right);
    extent.inkBottom = static_cast<int32_t>(bottom);
    return {};
  }
  if (const auto error = LayoutFontText(font, style, text, size, run); error)
    return error;
  int64_t above = metrics.ascent;
  int64_t below = metrics.descent;
  for (const auto &glyph: run.glyphs) {
    if (glyph->top > above) above = glyph->top;
    const int64_t glyphBelow = static_cast<int64_t>(glyph->height) - glyph->top;
    if (glyphBelow > below) below = glyphBelow;
  }
  const uint64_t metricHeight = static_cast<uint64_t>(metrics.ascent) +
                                metrics.descent + metrics.lineGap;
  const uint64_t safeHeight = static_cast<uint64_t>(above) +
                              static_cast<uint64_t>(below);
  const uint64_t lineHeight = metricHeight > safeHeight ? metricHeight : safeHeight;
  if (lineHeight > UINT32_MAX || above > UINT32_MAX) return kernel::KernelError::OutOfRange;
  metrics.lineHeight = static_cast<uint32_t>(
      lineHeight > metrics.lineHeight ? lineHeight : metrics.lineHeight);
  metrics.baselineOffset = static_cast<uint32_t>(above);
  const uint64_t advance = layout == TextLayoutMode_LayoutUniformGrid
                               ? static_cast<uint64_t>(run.glyphs.size()) * metrics.cellWidth
                               : static_cast<uint64_t>(run.width);
  if (advance > UINT32_MAX) return kernel::KernelError::OutOfRange;
  int64_t left = INT64_MAX, right = INT64_MIN, top = INT64_MAX, bottom = INT64_MIN, pen = 0;
  for (const auto &glyph: run.glyphs) {
    int64_t glyphX = pen + glyph->left;
    if (layout == TextLayoutMode_LayoutUniformGrid && glyph->left < 0)
      glyphX = pen - glyph->left;
    const int64_t glyphTop = static_cast<int64_t>(metrics.baselineOffset) - glyph->top;
    const int64_t glyphRight = glyphX + glyph->width;
    const int64_t glyphBottom = glyphTop + glyph->height;
    if (glyphX < left) left = glyphX;
    if (glyphRight > right) right = glyphRight;
    if (glyphTop < top) top = glyphTop;
    if (glyphBottom > bottom) bottom = glyphBottom;
    pen += layout == TextLayoutMode_LayoutUniformGrid
               ? metrics.cellWidth
               : static_cast<uint32_t>(glyph->advance);
  }
  if (left == INT64_MAX) left = right = top = bottom = 0;
  if (left < INT32_MIN || right > INT32_MAX || top < INT32_MIN || bottom > INT32_MAX)
    return kernel::KernelError::OutOfRange;
  extent.generation = font.generation;
  extent.advanceWidth = static_cast<uint32_t>(advance);
  extent.lineHeight = metrics.lineHeight;
  extent.baselineOffset = metrics.baselineOffset;
  extent.inkLeft = static_cast<int32_t>(left);
  extent.inkTop = static_cast<int32_t>(top);
  extent.inkRight = static_cast<int32_t>(right);
  extent.inkBottom = static_cast<int32_t>(bottom);
  return {};
}

struct OwnedBitmap final : kernel::KSurface {
  using KSurface::KSurface;
  void *pixels = nullptr;
  ~OwnedBitmap() override {
    if (pixels) KE_VMM_FreeRegion(KE_VMM_GetKernelAddressSpace(), pixels);
  }
};

kernel::user::URect DrawBounds(int64_t l, int64_t t, int64_t r, int64_t b) {
  const auto clamp = [](int64_t v) {
    return static_cast<int32_t>(v < INT32_MIN ? INT32_MIN : v > INT32_MAX ? INT32_MAX
                                                                          : v);
  };
  return {.left = clamp(l), .top = clamp(t), .right = clamp(r), .bottom = clamp(b)};
}

// USER's scene lock serializes screen writes with cursor save/restore and
// nonclient repaint. It must be taken before any per-DC lock.
// The cursor is touched only when the draw can reach it: the slot targets the
// cursor's surface and its clip and draw bounds overlap the cursor. Its restore and
// redraw then share a single present.
struct ScreenDrawGuard {
  kernel::user::Guard scene;
  kernel::user::URect restored{};
  // Call with the slot locked, before any pixel is written. `draw` (target
  // coordinates) narrows the test to the pixels the operation can touch.
  void exclude(const kernel::KDeviceContextSlot &slot, const kernel::user::URect *draw = nullptr) {
    if (!restored.Empty() || !slot.target) return;
    kernel::user::URect bounds{.left = 0, .top = 0, .right = static_cast<int32_t>(slot.target->width), .bottom = static_cast<int32_t>(slot.target->height)};
    if (slot.clipped) {
      kernel::user::URect clip{};
      for (uint32_t i = 0; slot.clipRects && i < slot.clipCount; ++i) {
        const auto &c = slot.clipRects[i];
        clip = clip.Union({.left = c.left, .top = c.top, .right = c.right, .bottom = c.bottom});
      }
      bounds = bounds.Intersect(clip);
    }
    if (draw) bounds = bounds.Intersect(*draw);
    if (bounds.Empty()) return;
    restored = kernel::user::cursor_exclude_locked(slot.target.get(), bounds);
  }
  ~ScreenDrawGuard() { kernel::user::cursor_include_locked(restored); }
};

uint32_t BytesPerPixel(kernel::PixelFormat format) {
  switch (format) {
    case kernel::PixelFormat::RGB24:
      return 3;
    case kernel::PixelFormat::ARGB32:
      return 4;
    case kernel::PixelFormat::RGB15:
    case kernel::PixelFormat::RGB16:
      return 2;
    case kernel::PixelFormat::INDEX8:
    default:
      return 1;
  }
}

kernel::KProcessObject *CurrentProcess() {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return nullptr;
  return currentThread->owner_process;
}

kstd::expected<kernel::KObjectPtr<kernel::KSurface>, kstd::error_code>
ResolveSurface(kernel::KProcessObject &proc, uint32_t handle) {
  const auto entry = proc.handles.Lookup(static_cast<kernel::KHandle>(handle));
  if (!entry) return kstd::unexpected(kernel::KernelError::InvalidHandle);
  const auto surface = entry->object.As<kernel::KSurface>();
  if (!surface) return kstd::unexpected(kernel::KernelError::WrongObjectType);
  return surface;
}

// Calls fn(left, top, right, bottom) for each part of the rectangle that lies
// inside the slot's clip list (or once, unclipped, when the DC has none).
template<typename Fn>
void ForEachClip(const kernel::KDeviceContextSlot &slot, int64_t l, int64_t t, int64_t r, int64_t b, Fn &&fn) {
  if (!slot.target) return;
  if (l < 0) l = 0;
  if (t < 0) t = 0;
  if (r > slot.target->width) r = slot.target->width;
  if (b > slot.target->height) b = slot.target->height;
  if (r <= l || b <= t) return;
  if (!slot.clipped) {
    fn(static_cast<int32_t>(l), static_cast<int32_t>(t), static_cast<int32_t>(r), static_cast<int32_t>(b));
    return;
  }
  for (uint32_t i = 0; i < slot.clipCount; ++i) {
    const auto &c = slot.clipRects[i];
    const int32_t cl = static_cast<int32_t>(l > c.left ? l : c.left);
    const int32_t ct = static_cast<int32_t>(t > c.top ? t : c.top);
    const int32_t cr = static_cast<int32_t>(r < c.right ? r : c.right);
    const int32_t cb = static_cast<int32_t>(b < c.bottom ? b : c.bottom);
    if (cr > cl && cb > ct) fn(cl, ct, cr, cb);
  }
}

// KSurface::pitch is the signed Win2k lDelta, so a bottom-up surface carries a
// negative stride; the byte size of the pixel store uses its magnitude.
size_t SurfaceSizeInBytes(const kernel::KSurface &surface) {
  const auto stride = surface.pitch < 0 ? -static_cast<int64_t>(surface.pitch) : surface.pitch;
  return static_cast<size_t>(stride) * static_cast<size_t>(surface.height);
}

bool InClip(const kernel::KDeviceContextSlot &slot, int64_t x, int64_t y) {
  if (!slot.target || x < 0 || y < 0 || x >= slot.target->width || y >= slot.target->height) return false;
  if (!slot.clipped) return true;
  for (uint32_t i = 0; i < slot.clipCount; ++i) {
    const auto &c = slot.clipRects[i];
    if (x >= c.left && x < c.right && y >= c.top && y < c.bottom) return true;
  }
  return false;
}

// Adapter-memory surfaces (Step 13) have no CPU backing and are never packed.
bool IsPacked(const kernel::KSurface &surface) {
  return surface.backing && (surface.type == kernel::SurfaceType::OffscreenBitmap ||
                             (surface.flags & kernel::SurfaceFlags::Linear) != 0);
}

uint32_t NativeColor(const kernel::KSurface &surface, uint32_t index) {
  const auto rgb = kernel::gfx::Palette16Rgb(index);
  switch (surface.pixelFormat) {
    case kernel::PixelFormat::INDEX8:
      return surface.type == kernel::SurfaceType::Framebuffer && IsPacked(surface) &&
                     surface.bpp != 4
                 ? kernel::gfx::RgbTo332(rgb)
                 : index;
    case kernel::PixelFormat::RGB15: return kernel::gfx::RgbTo555(rgb);
    case kernel::PixelFormat::RGB16: return kernel::gfx::RgbTo565(rgb);
    case kernel::PixelFormat::ARGB32: return 0xff000000u | rgb;
    case kernel::PixelFormat::RGB24: return rgb;
  }
  return 0;
}

struct DirtyRect {
  kernel::KClipRect rect{.left = INT32_MAX, .top = INT32_MAX, .right = 0, .bottom = 0};
  void Add(int32_t l, int32_t t, int32_t r, int32_t b) {
    if (l < rect.left) rect.left = l;
    if (t < rect.top) rect.top = t;
    if (r > rect.right) rect.right = r;
    if (b > rect.bottom) rect.bottom = b;
  }
};

// Adapter allowed to draw on `surface`: none for system-RAM surfaces, a
// Terminated adapter or a lost surface (Step 13).
kernel::KGraphicsAdapter *DrawAdapter(const kernel::KSurface &surface) {
  const auto &adapter = surface.adapter;
  if (!adapter || adapter->state == kernel::KGraphicsAdapter::State::Terminated ||
      (surface.flags & kernel::SurfaceFlags::Lost))
    return nullptr;
  return adapter.get();
}

// CPU-backed, or adapter memory with a usable adapter.
bool Drawable(const kernel::KDeviceContextSlot &slot) {
  return slot.target && (slot.target->backing || DrawAdapter(*slot.target));
}

bool IsScreen(const kernel::KSurface &surface) {
  return surface.display && surface.display->surface.get() == &surface;
}

kstd::error_code Fill(kernel::KDeviceContextSlot &slot, int64_t l, int64_t t, int64_t r, int64_t b,
                      uint32_t color, DirtyRect &dirty) {
  if (!Drawable(slot)) return kernel::KernelError::InvalidState;
  kstd::error_code result = kernel::KernelError::Success;
  const auto native = NativeColor(*slot.target, color);
  ForEachClip(slot, l, t, r, b, [&](int32_t cl, int32_t ct, int32_t cr, int32_t cb) {
    kstd::error_code drawn = kernel::KernelError::Unsupported;
    bool visible = false;
    const bool hasAdapter = DrawAdapter(*slot.target) != nullptr;
    const auto &adapter = slot.target->adapter;
    const bool packed = IsPacked(*slot.target);
    const bool tiny = static_cast<int64_t>(cr - cl) * (cb - ct) <= 256;
    if (packed && (tiny || slot.noAccel))
      drawn = KE_GFX_SoftwareFillRect(slot.target, cl, ct, cr - cl, cb - ct, native);
    if (drawn && hasAdapter && adapter->FillRect && (!slot.noAccel || !packed)) {
      drawn = adapter->FillRect(adapter, slot.target, cl, ct, cr - cl, cb - ct, native);
      // The adapter already wrote the visible image; presenting would re-upload it.
      visible = !drawn && kernel::Any(adapter->caps & kernel::KGfxCaps::VisibleFill);
    }
    if (drawn && packed)
      drawn = KE_GFX_SoftwareFillRect(slot.target, cl, ct, cr - cl, cb - ct, native);
    if (!drawn && !visible) dirty.Add(cl, ct, cr, cb);
    if (!result) result = drawn;
  });
  return result;
}

void Present(kernel::KDeviceContextSlot &slot, const DirtyRect &dirty) {
  // BeginPaint DCs are flushed once by USER EndPaint, not once per icon span
  // or text label. Unrestricted screen DCs still present synchronously.
  if (slot.clipped && slot.hwnd) {
    if (dirty.rect.right > dirty.rect.left && dirty.rect.bottom > dirty.rect.top) {
      if (!slot.presentationDirty) {
        slot.presentationBounds = dirty.rect;
        slot.presentationDirty = true;
      } else {
        auto &bounds = slot.presentationBounds;
        if (dirty.rect.left < bounds.left) bounds.left = dirty.rect.left;
        if (dirty.rect.top < bounds.top) bounds.top = dirty.rect.top;
        if (dirty.rect.right > bounds.right) bounds.right = dirty.rect.right;
        if (dirty.rect.bottom > bounds.bottom) bounds.bottom = dirty.rect.bottom;
      }
    }
    return;
  }
  if (dirty.rect.right > dirty.rect.left && dirty.rect.bottom > dirty.rect.top &&
      IsScreen(*slot.target) && DrawAdapter(*slot.target) && slot.target->adapter->Present)
    slot.target->adapter->Present(slot.target->adapter, slot.target, &dirty.rect, 1);
}

struct TextRenderState {
  kernel::KObjectPtr<kernel::KObject> font;
  uint32_t hwnd{};
  kernel::KObjectPtr<kernel::KSurface> target;
  kernel::KObjectPtr<kernel::KDisplay> display;
  kernel::KObjectPtr<kernel::KMemoryBackingObject> backing;
  kernel::KClipRect clips[kernel::user::URegion::kMaxRects]{};
  uint64_t generation{};
  uint32_t clipCount{}, foreground{}, background{};
  int32_t originX{}, originY{}, pitch{};
  uint32_t width{}, height{}, bpp{};
  kernel::PixelFormat format{};
  uint32_t flags{};
  size_t size{};
  kernel::VirtualAddress address{};
  size_t backingSize{};
  kernel::KObjectPtr<kernel::KSurface> displaySurface;
  kernel::KObjectPtr<kernel::KGraphicsAdapter> adapter;
  uint32_t displayGeneration{};
  bool attached{}, usable{};
  bool clipped{}, opaque{}, noAccel{};

  kstd::error_code capture(const kernel::KDeviceContextSlot &slot) {
    if (!slot.target || !slot.target->backing)
      return kernel::KernelError::InvalidState;
    if (slot.clipCount > kernel::user::URegion::kMaxRects ||
        (slot.clipCount && !slot.clipRects))
      return kernel::KernelError::ResourceLimitExceeded;
    font = slot.selectedFont;
    hwnd = slot.hwnd;
    target = slot.target;
    display = target->display;
    backing = target->backing;
    generation = slot.renderGeneration;
    clipCount = slot.clipCount;
    for (uint32_t i = 0; i < clipCount; ++i) clips[i] = slot.clipRects[i];
    foreground = slot.penColor;
    background = slot.backgroundColor;
    originX = slot.originX;
    originY = slot.originY;
    clipped = slot.clipped;
    opaque = slot.opaqueText;
    noAccel = slot.noAccel;
    width = target->width;
    height = target->height;
    pitch = target->pitch;
    bpp = target->bpp;
    format = target->pixelFormat;
    flags = target->flags;
    size = target->sizeInBytes;
    address = backing->directAddress;
    backingSize = backing->size;
    // Same adapter source as every other draw path (Step 13a).
    adapter = target->adapter;
    usable = DrawAdapter(*target) != nullptr;
    if (display) {
      displaySurface = display->surface;
      displayGeneration = display->modeGeneration;
      attached = display->isAttachedToDesktop;
    }
    return {};
  }

  bool matches(const kernel::KDeviceContextSlot &slot) const {
    if (slot.renderGeneration != generation || slot.selectedFont != font ||
        slot.target != target || target->display != display ||
        slot.hwnd != hwnd || slot.clipCount != clipCount ||
        slot.penColor != foreground || slot.backgroundColor != background ||
        slot.originX != originX || slot.originY != originY ||
        slot.clipped != clipped || slot.opaqueText != opaque ||
        slot.noAccel != noAccel || target->backing != backing ||
        target->width != width || target->height != height || target->pitch != pitch ||
        target->bpp != bpp || target->pixelFormat != format ||
        target->flags != flags || target->sizeInBytes != size ||
        backing->directAddress != address || backing->size != backingSize ||
        target->adapter != adapter || (usable && !DrawAdapter(*target)) ||
        (display && (display->surface != displaySurface ||
                     display->modeGeneration != displayGeneration ||
                     display->isAttachedToDesktop != attached)))
      return false;
    for (uint32_t i = 0; i < clipCount; ++i) {
      if (!slot.clipRects) return false;
      const auto &a = clips[i], &b = slot.clipRects[i];
      if (a.left != b.left || a.top != b.top ||
          a.right != b.right || a.bottom != b.bottom) return false;
    }
    return true;
  }
};

struct TextCursor {
  uint64_t backgroundPixel{};
  size_t glyph{}, byte{};
  uint32_t row{}, column{};
  int64_t pen{};
};

// Scanned off-clip bits count too. A lit bit and its fill pixel consume two
// units, so neither scanning nor submitted fill work can exceed the limit.
kstd::error_code CommitTextSlice(
    kernel::KDeviceContextSlot &slot, const kernel::font::FontObject &font,
    const kernel::font::TextRun &run, const TextMetricsV2 &metrics,
    const TextExtentV2 &extent, const char *text, size_t size,
    uint32_t layout, int64_t x, int64_t y, TextCursor &cursor,
    bool &done, uint32_t &processed, uint32_t &runs, DirtyRect &dirty) {
  constexpr uint32_t PixelLimit = 256, RunLimit = 32;
  const uint64_t backgroundPixels = slot.opaqueText
                                        ? uint64_t{extent.advanceWidth} * metrics.lineHeight
                                        : 0;
  while (processed < PixelLimit && runs < RunLimit &&
         cursor.backgroundPixel < backgroundPixels) {
    const auto row = static_cast<int64_t>(cursor.backgroundPixel / extent.advanceWidth);
    const auto column = static_cast<uint32_t>(cursor.backgroundPixel % extent.advanceWidth);
    uint32_t count = extent.advanceWidth - column;
    if (count > PixelLimit - processed) count = PixelLimit - processed;
    const auto error = Fill(slot, x + column, y + row,
                            x + column + count, y + row + 1,
                            slot.backgroundColor, dirty);
    cursor.backgroundPixel += count;
    processed += count;
    ++runs;
    if (error) return error;
  }
  while (processed + 2 <= PixelLimit && runs < RunLimit) {
    const kernel::font::GlyphMask *mask = nullptr;
    uint32_t width = 0, height = 0;
    int64_t advance = 0, left = 0, top = 0;
    uint32_t character = 0;
    if (font.bitmap) {
      if (cursor.byte == size) {
        done = true;
        break;
      }
      const auto step = kernel::font::decode_utf8(
          reinterpret_cast<const uint8_t *>(text) + cursor.byte, size - cursor.byte);
      character = !is_control_codepoint(step.codepoint) &&
                          step.codepoint >= kernel::gfx::kFontFirstChar &&
                          step.codepoint <= kernel::gfx::kFontLastChar
                      ? step.codepoint
                      : kernel::gfx::kFontDefaultChar;
      width = metrics.cellWidth;
      advance = metrics.cellWidth;
      height = metrics.lineHeight;
      top = metrics.baselineOffset;
    } else {
      if (cursor.glyph == run.glyphs.size()) {
        done = true;
        break;
      }
      mask = run.glyphs[cursor.glyph].get();
      width = mask->width;
      height = mask->height;
      left = mask->left;
      top = mask->top;
      advance = layout == TextLayoutMode_LayoutUniformGrid
                    ? int64_t{metrics.cellWidth}
                    : int64_t{mask->advance};
    }
    if (!width || cursor.row == height) {
      ++processed;
      cursor.pen += advance;
      cursor.row = cursor.column = 0;
      ++cursor.glyph;
      if (font.bitmap) {
        const auto step = kernel::font::decode_utf8(
            reinterpret_cast<const uint8_t *>(text) + cursor.byte, size - cursor.byte);
        cursor.byte += step.length;
      }
      continue;
    }
    auto bit = [&](uint32_t column) {
      if (font.bitmap) {
        return cursor.row < 14 && column >= 1 && column < 6 &&
               (kernel::gfx::BitmapGlyphRow(static_cast<uint8_t>(character), cursor.row / 2) &
                (16u >> (column - 1))) != 0;
      }
      return (mask->bits[static_cast<size_t>(cursor.row) * mask->stride +
                         column / 8] &
              (0x80u >> (column % 8))) != 0;
    };
    uint32_t first = cursor.column, end = cursor.column;
    bool ink = false;
    while (cursor.column < width && processed + 2 <= PixelLimit) {
      const bool lit = bit(cursor.column);
      ++processed;
      if (lit) {
        if (!ink) first = cursor.column;
        ink = true;
        ++processed;
        end = cursor.column + 1;
      }
      ++cursor.column;
      if (!lit && ink) break;
    }
    if (ink) {
      const int64_t glyphLeft = layout == TextLayoutMode_LayoutUniformGrid
                                    ? cursor.pen - (left < 0 ? left : 0)
                                    : cursor.pen + left;
      const auto error = Fill(slot, x + glyphLeft + first,
                              y + metrics.baselineOffset - top + cursor.row,
                              x + glyphLeft + end,
                              y + metrics.baselineOffset - top + cursor.row + 1,
                              slot.penColor, dirty);
      ++runs;
      if (error) return error;
    }
    if (cursor.column == width) {
      cursor.column = 0;
      ++cursor.row;
    }
  }
  return {};
}

}// namespace

kstd::error_code handle_BeginFontUpload_V1(__KRN_GFX_BEGINFONTUPLOAD_V1 &&args) {
  args.upload = 0;
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  auto *fonts = &kernel::gfx::font_resources();
  const auto upload = fonts->begin(fontResourceBudget, args.totalBytes, process->pid, process->incarnation);
  if (!upload) return upload.error();
  args.upload = *upload;
  return {};
}

kstd::error_code handle_AppendFontUpload_V1(__KRN_GFX_APPENDFONTUPLOAD_V1 &&args) {
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  auto *fonts = &kernel::gfx::font_resources();
  return fonts->append(args.upload, args.offset, args.data.get(), args.data.size());
}

kstd::error_code handle_CommitFontUpload_V1(__KRN_GFX_COMMITFONTUPLOAD_V1 &&args) {
  args.face = 0;
  args.validationError = 0;
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  auto *system = fontSystem();
  if (!system) return kernel::KernelError::OutOfMemory;
  auto *fonts = &kernel::gfx::font_resources();
  const auto face = fonts->commit(args.upload, system->faces, args.validationError);
  if (!face) {
    if (face.error() == kernel::KernelError::InvalidResource)
      KE_Print("GFX Font Engine: registration rejected: {}",
               undos::font::error_message(static_cast<undos::font::Error>(args.validationError)));
    return face.error();
  }
  args.face = *face;
  return {};
}

kstd::error_code handle_CloseFontResource_V1(__KRN_GFX_CLOSEFONTRESOURCE_V1 &&args) {
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  auto *fonts = &kernel::gfx::font_resources();
  return fonts->close(args.handle);
}

kstd::error_code handle_SetSystemFont_V1(__KRN_GFX_SETSYSTEMFONT_V1 &&args) {
  args.generation = 0;
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  auto *system = fontSystem();
  if (!system) return kernel::KernelError::OutOfMemory;
  kstd::error_code result;
  if (args.backend == FontBackend_BackendBuiltinBitmap) {
    if (args.regularFace || args.boldFace || args.ppem)
      return kernel::KernelError::InvalidArgument;
    result = kernel::user::set_builtin_default_font(system->defaults);
  } else if (args.backend == FontBackend_BackendTrueType) {
    auto *fonts = &kernel::gfx::font_resources();
    const uint32_t regular = fonts->registry_handle(args.regularFace);
    const uint32_t bold = fonts->registry_handle(args.boldFace);
    if (!regular || !bold) return kernel::KernelError::InvalidHandle;
    result = kernel::user::set_default_font(system->defaults, regular, bold, args.ppem);
  } else {
    return kernel::KernelError::InvalidArgument;
  }
  if (result) return result;
  args.generation = system->defaults.snapshot().generation;
  return {};
}

kstd::error_code handle_AcquireFont_V1(__KRN_GFX_ACQUIREFONT_V1 &&args) {
  args.font = args.generation = 0;
  if (args.source != FontSource_AcquireSystemDefault &&
      args.source != FontSource_AcquireBuiltinBitmap)
    return kernel::KernelError::InvalidArgument;
  if (args.fallback != GlyphFallbackPolicy_FallbackFaceNotdef &&
      args.fallback != GlyphFallbackPolicy_FallbackBitmapForUnmapped)
    return kernel::KernelError::InvalidArgument;
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  auto *system = fontSystem();
  if (!system) return kernel::KernelError::OutOfMemory;
  auto created = kernel::CreateAnonymousKObject<kernel::font::FontObject>();
  if (!created) return created.error();
  auto object = kstd::move(*created);
  object->generation = system->defaults.snapshot().generation;
  object->fallback = static_cast<uint32_t>(args.fallback);
  if (args.source == FontSource_AcquireSystemDefault) {
    object->snapshot = system->defaults.snapshot();
    object->bitmap = !object->snapshot || object->snapshot.bitmapBackend;
  }
  args.generation = object->generation;
  const auto handle = process->handles.Allocate(
      kernel::KObjectPtr<kernel::KObject>(object.get()),
      kernel::ProcessHandleTable::HandleAccess::Read |
          kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::None);
  if (!handle) return handle.error();
  args.font = static_cast<uint32_t>(*handle);
  return {};
}

kstd::error_code handle_CloseFont_V1(__KRN_GFX_CLOSEFONT_V1 &&args) {
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  if (!ResolveFont(*process, args.font)) return kernel::KernelError::InvalidHandle;
  return process->handles.Close(static_cast<kernel::KHandle>(args.font));
}

kstd::error_code handle_SelectFont_V1(__KRN_GFX_SELECTFONT_V1 &&args) {
  args.previousFont = 0;
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  auto font = ResolveFont(*process, args.font);
  if (!font) return kernel::KernelError::InvalidHandle;
  // Reserve the previous-font handle before any lock: the handle table may
  // grow, and binding the reservation later cannot fail or allocate.
  const auto reserved = process->handles.Reserve(
      kernel::ProcessHandleTable::HandleAccess::Read |
          kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::None);
  if (!reserved) return reserved.error();
  const kernel::KObjectPtr<kernel::KObject> selected(font.get());
  kernel::KObjectPtr<kernel::KObject> previous;
  bool resolved = false;
  {
    kernel::user::Guard scene;
    // Resolve under the scene lock so a concurrent DeleteDC or slot reuse is
    // seen through the generation check.
    if (auto *slot = kernel::gfx::FindDC(process, args.dc)) {
      resolved = true;
      if (!slot->ChangeRenderState()) {
        (void) process->handles.Cancel(*reserved);
        return kernel::KernelError::InvalidState;
      }
      previous = slot->selectedFont;
      slot->selectedFont = selected;
    }
  }
  if (!resolved || !previous) {
    (void) process->handles.Cancel(*reserved);
    if (!resolved) return kernel::KernelError::InvalidHandle;
    return {};
  }
  // Only this call holds the token, and user Close cannot remove a
  // reservation, so Bind cannot fail here; report it if the invariant breaks.
  const auto bound = process->handles.Bind(*reserved, previous);
  if (!bound) return kernel::KernelError::InvalidState;
  args.previousFont = static_cast<uint32_t>(*bound);
  return {};
}

kstd::error_code handle_GetFontMetrics_V1(__KRN_GFX_GETFONTMETRICS_V1 &&args) {
  args.metrics = {};
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  auto font = ResolveFont(*process, args.font);
  if (!font) return kernel::KernelError::InvalidHandle;
  if (args.repertoire != TextRepertoire_RepertoireBasic &&
      args.repertoire != TextRepertoire_RepertoireTerminal)
    return kernel::KernelError::InvalidArgument;
  return FontMetrics(*font, static_cast<uint32_t>(args.style),
                     static_cast<uint32_t>(args.repertoire), args.metrics);
}

kstd::error_code handle_MeasureText_V1(__KRN_GFX_MEASURETEXT_V1 &&args) {
  TextEntryState entry;
  args.extent = {};
  const size_t size = args.text.size();
  if (const auto error = ValidateText(args.style, args.layout, args.repertoire,
                                      args.text.data(), size);
      error) return error;
  char text[kernel::font::MaxTextBytes];
  if (size) kstd::memcpy(text, args.text.data(), size);
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  auto font = ResolveFont(*process, args.font);
  if (!font) return kernel::KernelError::InvalidHandle;
  if (args.repertoire != TextRepertoire_RepertoireBasic &&
      args.repertoire != TextRepertoire_RepertoireTerminal)
    return kernel::KernelError::InvalidArgument;
  kstd::error_code result;
  {
    TextInterruptWindow window;
    kernel::font::TextRun run;
    TextMetricsV2 metrics{};
    result = PrepareFontText(*font, static_cast<uint32_t>(args.style),
                             static_cast<uint32_t>(args.layout),
                             static_cast<uint32_t>(args.repertoire), text,
                             size, run, metrics, args.extent);
    font = nullptr;
  }
  return result;
}

kstd::error_code handle_CreateDC_V1(__KRN_GFX_CREATEDC_V1 &&args) {
  args.handle = 0;
  (void) args.driverName;
  (void) args.deviceName;
  const auto flags = static_cast<uint32_t>(args.initFlags);
  constexpr uint32_t supported = DCInitFlags_Primary | DCInitFlags_Memory | DCInitFlags_Offscreen | DCInitFlags_NoAccel;
  const bool memory = (flags & (DCInitFlags_Memory | DCInitFlags_Offscreen)) != 0;
  if ((flags & ~supported) || (memory && (flags & DCInitFlags_Primary))) return kernel::KernelError::InvalidArgument;

  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  {
    kernel::user::Guard scene;
    const auto display = kernel::gfx::active_video_present_target();
    if (!memory && (!display || !display->surface || !display->isAttachedToDesktop))
      return kernel::KernelError::Unsupported;
  }
  const auto allocated = kernel::gfx::CreateDC(proc);
  if (!allocated) return allocated.error();
  auto [slot, hdc] = *allocated;
  kernel::user::Guard scene;
  const auto display = kernel::gfx::active_video_present_target();
  if (!memory && (!display || !display->surface || !display->isAttachedToDesktop)) {
    kernel::gfx::CloseDC(proc, hdc);
    return kernel::KernelError::Unsupported;
  }

  slot->memoryDC = memory;
  slot->noAccel = (flags & DCInitFlags_NoAccel) != 0;
  slot->target = memory ? nullptr : display->surface;

  args.handle = hdc;
  return {};
}

namespace {
kernel::gfx::VideoPresentPathInfo InternalPath(const VideoPresentPath &path) {
  return {
      .sourceId = path.sourceId,
      .sourceModeId = path.sourceModeId,
      .targetId = path.targetId,
      .targetModeId = path.targetModeId,
      .sourceX = path.sourceX,
      .sourceY = path.sourceY,
      .width = path.width,
      .height = path.height,
      .flags = path.flags};
}

VideoPresentPath PublicPath(const kernel::gfx::VideoPresentPathInfo &path) {
  return {
      .sourceId = path.sourceId,
      .sourceModeId = path.sourceModeId,
      .targetId = path.targetId,
      .targetModeId = path.targetModeId,
      .sourceX = path.sourceX,
      .sourceY = path.sourceY,
      .width = path.width,
      .height = path.height,
      .flags = path.flags};
}

template<typename T, typename GetRecord, typename Convert>
kstd::error_code EnumerateVideoRecords(uint32_t firstIndex, uint32_t queryOnly,
                                       kernel::borrowed_ptr<T> &output,
                                       size_t suppliedCapacity,
                                       uint32_t total, uint32_t &required, uint32_t &written,
                                       GetRecord &&getRecord, Convert &&convert) {
  written = 0;
  required = 0;
  if (queryOnly > 1 || (queryOnly && (suppliedCapacity || output.size())))
    return kernel::KernelError::InvalidArgument;
  if (total > kernel::gfx::VideoPresentRecordLimit)
    return kernel::KernelError::ResourceLimitExceeded;
  if (firstIndex > total) return kernel::KernelError::InvalidArgument;
  required = total - firstIndex;
  if (queryOnly) return {};
  if (output.size() < required) return kernel::KernelError::BufferTooSmall;
  if (required && !output) return kernel::KernelError::InvalidAddress;
  for (size_t index = 0; index < required; ++index) {
    const auto record = getRecord(firstIndex + static_cast<uint32_t>(index));
    convert(record, output.get()[index]);
  }
  written = static_cast<uint32_t>(required);
  return {};
}
}// namespace

kstd::error_code handle_GetVideoPresentTopology_V1(__KRN_GFX_GETVIDEOPRESENTTOPOLOGY_V1 &&args) {
  args.topology = {};
  kernel::gfx::VideoPresentTopologyInfo topology{};
  if (const auto error = kernel::gfx::get_video_present_topology(topology); error) return error;
  args.topology.generation = topology.generation;
  args.topology.sourceCount = topology.sourceCount;
  args.topology.sourceModeCount = topology.sourceModeCount;
  args.topology.targetCount = topology.targetCount;
  args.topology.targetModeCount = topology.targetModeCount;
  args.topology.pathCount = topology.pathCount;
  args.topology.recordLimit = kernel::gfx::VideoPresentRecordLimit;
  args.topology.degraded = topology.degraded;
  return {};
}

kstd::error_code handle_EnumVideoPresentSourceModes_V1(__KRN_GFX_ENUMVIDEOPRESENTSOURCEMODES_V1 &&args) {
  args.required = args.written = 0;
  if (!args.generation) return kernel::KernelError::Retry;
  const auto snapshot = args.modes
                            ? kernel::gfx::acquire_published_video_present_snapshot(args.generation)
                            : kernel::gfx::acquire_video_present_snapshot(args.generation);
  if (!snapshot) return snapshot.error();
  if (args.sourceId != kernel::gfx::DesktopSourceId) return kernel::KernelError::NotFound;
  return EnumerateVideoRecords(args.firstIndex, args.queryOnly, args.modes, args.modes_capacity, (*snapshot)->topology.sourceModeCount, args.required, args.written, [&](uint32_t index) { return (*snapshot)->sourceModes[index]; }, [](const kernel::gfx::VideoPresentSourceModeInfo &mode, VideoPresentSourceMode &out) { out = {.sourceId = kernel::gfx::DesktopSourceId, .modeId = mode.modeId, .width = mode.width, .height = mode.height, .colorDepth = mode.colorDepth, .pixelFormat = static_cast<uint32_t>(mode.pixelFormat)}; });
}

kstd::error_code handle_EnumVideoPresentTargets_V1(__KRN_GFX_ENUMVIDEOPRESENTTARGETS_V1 &&args) {
  args.required = args.written = 0;
  if (!args.generation) return kernel::KernelError::Retry;
  const auto snapshot = args.targets
                            ? kernel::gfx::acquire_published_video_present_snapshot(args.generation)
                            : kernel::gfx::acquire_video_present_snapshot(args.generation);
  if (!snapshot) return snapshot.error();
  return EnumerateVideoRecords(args.firstIndex, args.queryOnly, args.targets, args.targets_capacity, (*snapshot)->topology.targetCount, args.required, args.written, [&](uint32_t index) { return (*snapshot)->targets[index]; }, [](const kernel::gfx::VideoPresentTargetInfo &target, VideoPresentTarget &out) { out = {.targetId = target.targetId, .connected = target.connected ? 1u : 0u, .modeCount = target.modeCount, .maxWidth = target.maxWidth, .maxHeight = target.maxHeight, .maxRefreshHz = target.maxRefreshHz, .adaptiveSyncSupported = 0, .adaptiveSyncMinMilliHz = 0, .adaptiveSyncMaxMilliHz = 0}; });
}

kstd::error_code handle_EnumVideoPresentTargetModes_V1(__KRN_GFX_ENUMVIDEOPRESENTTARGETMODES_V1 &&args) {
  args.required = args.written = 0;
  if (!args.generation) return kernel::KernelError::Retry;
  const auto snapshot = args.modes
                            ? kernel::gfx::acquire_published_video_present_snapshot(args.generation)
                            : kernel::gfx::acquire_video_present_snapshot(args.generation);
  if (!snapshot) return snapshot.error();
  uint32_t total = 0;
  uint32_t offset = 0;
  bool found = false;
  for (uint32_t index = 0; index < (*snapshot)->topology.targetCount; ++index) {
    const auto &target = (*snapshot)->targets[index];
    if (target.targetId == args.targetId) {
      total = target.modeCount;
      found = true;
      break;
    }
    offset += target.modeCount;
  }
  if (!found) return kernel::KernelError::NotFound;
  return EnumerateVideoRecords(args.firstIndex, args.queryOnly, args.modes, args.modes_capacity, total, args.required, args.written, [&](uint32_t index) { return (*snapshot)->targetModes[offset + index]; }, [](const kernel::gfx::VideoPresentTargetModeInfo &mode, VideoPresentTargetMode &out) { out = {.targetId = mode.targetId, .modeId = mode.modeId, .width = mode.mode.width, .height = mode.mode.height, .colorDepth = mode.mode.bpp, .pixelFormat = static_cast<uint32_t>(mode.mode.pixelFormat), .refreshHz = mode.mode.refreshRate}; });
}

kstd::error_code handle_EnumVideoPresentPaths_V1(__KRN_GFX_ENUMVIDEOPRESENTPATHS_V1 &&args) {
  args.required = args.written = 0;
  if (!args.generation) return kernel::KernelError::Retry;
  const auto snapshot = args.paths
                            ? kernel::gfx::acquire_published_video_present_snapshot(args.generation)
                            : kernel::gfx::acquire_video_present_snapshot(args.generation);
  if (!snapshot) return snapshot.error();
  if ((*snapshot)->topology.pathCount && !(*snapshot)->pathsValid)
    return kernel::KernelError::InvalidState;
  return EnumerateVideoRecords(args.firstIndex, args.queryOnly, args.paths, args.paths_capacity, (*snapshot)->topology.pathCount, args.required, args.written, [&](uint32_t index) { return (*snapshot)->paths[index]; }, [](const kernel::gfx::VideoPresentPathInfo &path, VideoPresentPath &out) { out = PublicPath(path); });
}

kstd::error_code handle_ValidateVideoPresentTopology_V1(__KRN_GFX_VALIDATEVIDEOPRESENTTOPOLOGY_V1 &&args) {
  args.token = 0;
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  if (args.paths_len > kernel::gfx::VideoPresentRecordLimit)
    return kernel::KernelError::ResourceLimitExceeded;
  if (args.paths_len != 1 || !args.paths) return kernel::KernelError::InvalidSize;

  const VideoPresentPath snapshot = args.paths.get()[0];
  const auto path = InternalPath(snapshot);
  auto created = kernel::CreateKObject<kernel::KVideoPresentValidationToken>("VideoPresentToken");
  if (!created) return created.error();
  const auto &token = *created;
  token->generation = args.generation;
  token->path = path;
  size_t driverModeIndex = 0;
  if (const auto error = kernel::gfx::video_present_prepare_path(
          args.generation, path, token->target, driverModeIndex,
          token->reservation, &token->snapshot);
      error)
    return error;
  const auto handle = process->handles.Allocate(
      token,
      kernel::ProcessHandleTable::HandleAccess::Read |
          kernel::ProcessHandleTable::HandleAccess::Write |
          kernel::ProcessHandleTable::HandleAccess::Close,
      kernel::ProcessHandleTable::HandleFlags::None);
  if (!handle) return handle.error();
  args.token = static_cast<uint32_t>(*handle);
  return {};
}

kstd::error_code handle_CommitVideoPresentTopology_V1(__KRN_GFX_COMMITVIDEOPRESENTTOPOLOGY_V1 &&args) {
  args.required = 1;
  args.written = 0;
  auto *process = CurrentProcess();
  if (!process) return kernel::KernelError::NoCurrentProcess;
  const auto handle = static_cast<kernel::KHandle>(args.token);
  auto *entry = process->handles.Lookup(handle);
  if (!entry || !entry->object) return kernel::KernelError::InvalidHandle;
  if (!process->handles.HasAccess(handle, kernel::ProcessHandleTable::HandleAccess::Read))
    return kernel::KernelError::AccessDenied;
  const auto token = entry->object.As<kernel::KVideoPresentValidationToken>();
  if (!token) return kernel::KernelError::WrongObjectType;
  bool firstActivation = false;
  const auto committed = kernel::gfx::commit_video_present_path(
      token->generation, token->path, token->target, token->reservation,
      args.results ? args.results.size() : 0, &firstActivation);
  if (!committed) {
    if (committed.error() != kernel::KernelError::OutOfMemory &&
        committed.error() != kernel::KernelError::BufferTooSmall)
      (void) process->handles.Close(handle);
    return committed.error();
  }
  (void) process->handles.Close(handle);
  auto &out = args.results.get()[0];
  out.targetId = committed->targetId;
  out.requestedModeId = committed->requestedModeId;
  out.actualModeId = committed->actualModeId;
  out.active = committed->active;
  out.outcome = static_cast<VideoPresentOutcome>(committed->outcome);
  out.degraded = committed->degraded;
  args.written = 1;

  kernel::user::Guard scene;
  kernel::user::SceneDrawGuard drawing;
  kernel::user::cursor_reset_locked();
  kernel::user::display_input_changed_locked(firstActivation);
  const auto active = kernel::gfx::active_video_present_target();
  kernel::user::display_changed_locked(active ? active->currentMode.bpp : 0);
  return {};
}

kstd::error_code handle_DeleteDC_V1(__KRN_GFX_DELETEDC_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  // Declared before the scene lock: the DC's owners are dropped after it.
  kernel::DetachedDC released;
  kernel::user::Guard scene;

  for (auto *window = kernel::user::topmost_locked(); window; window = window->below) {
    if (window->process == proc && window->paintDC == args.dc) {
      kernel::user::abandon_paint_locked(*window);
      break;
    }
  }

  return kernel::gfx::CloseDC(proc, args.dc, released);
}

kstd::error_code handle_SelectObject_V1(__KRN_GFX_SELECTOBJECT_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  kernel::KObjectPtr<kernel::KSurface> oldTarget;
  kernel::user::Guard scene;

  const auto slot = kernel::gfx::FindDC(proc, args.dc);
  if (!slot) return kernel::KernelError::InvalidHandle;

  const auto resolved = ResolveSurface(*proc, args.object);
  if (!resolved) return resolved.error();
  const auto &bitmap = *resolved;

  if (!slot->memoryDC) {
    return kernel::KernelError::InvalidState;
  }
  if (!slot->ChangeRenderState()) {
    return kernel::KernelError::InvalidState;
  }
  oldTarget = kstd::move(slot->target);
  slot->target = bitmap;

  return {};
}

kstd::error_code handle_CreatePen_V1(__KRN_GFX_CREATEPEN_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}

kstd::error_code handle_CreateBrush_V1(__KRN_GFX_CREATEBRUSH_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}

// Zero-initialized top-down packed bitmap in kernel RAM.
kstd::error_code CreateRamBitmap(uint32_t width, uint32_t height, kernel::PixelFormat pixelFormat,
                                 kernel::KObjectPtr<kernel::KSurface> &out) {
  if (width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX ||
      static_cast<uint32_t>(pixelFormat) > 2) return kernel::KernelError::InvalidArgument;

  const uint32_t bpp = BytesPerPixel(pixelFormat);
  const uint64_t widePitch = static_cast<uint64_t>(width) * bpp;
  const uint64_t wideSize = widePitch * height;
  // A bounded allocation protects the 32-bit kernel and low-memory targets.
  if (widePitch > INT32_MAX || wideSize > 16u * 1024u * 1024u) return kernel::KernelError::InvalidArgument;
  const auto pitch = static_cast<size_t>(widePitch);
  const auto size = static_cast<size_t>(wideSize);

  const auto createdBitmap = kernel::CreateKObject<OwnedBitmap>("Bitmap");
  if (!createdBitmap) return createdBitmap.error();
  const auto &bitmap = *createdBitmap;
  const auto createdBacking = kernel::CreateKObject<kernel::KMemoryBackingObject>("BitmapBacking");
  if (!createdBacking) return createdBacking.error();
  const auto &backing = *createdBacking;

  auto &as = KE_VMM_GetKernelAddressSpace();
  const auto allocatedRegion = KE_VMM_AllocateRegion(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!allocatedRegion) return allocatedRegion.error();
  void *region = *allocatedRegion;

  bitmap->pixels = region;
  backing->directAddress = kernel::VirtualAddress::from_ptr(region);
  backing->size = size;
  bitmap->backing = backing;
  bitmap->width = width;
  bitmap->height = height;
  bitmap->pitch = static_cast<int32_t>(pitch);
  bitmap->pixelFormat = pixelFormat;
  bitmap->bpp = bpp * 8;
  bitmap->sizeInBytes = size;
  bitmap->flags = kernel::SurfaceFlags::TopDown | kernel::SurfaceFlags::Linear;
  __builtin_memset(region, 0, size);
  out = bitmap;
  return {};
}

kstd::error_code PublishBitmap(kernel::KProcessObject &proc, const kernel::KObjectPtr<kernel::KSurface> &bitmap,
                               uint32_t &handle) {
  kernel::user::Guard scene;
  const auto allocated = proc.handles.Allocate(
      bitmap,
      kernel::ProcessHandleTable::HandleAccess::Read |
          kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::None);
  if (!allocated) return allocated.error();
  handle = static_cast<uint32_t>(*allocated);
  return {};
}

kstd::error_code handle_CreateBitmap_V1(__KRN_GFX_CREATEBITMAP_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;

  kernel::KObjectPtr<kernel::KSurface> bitmap;
  if (static_cast<uint32_t>(args.format) > 2) return kernel::KernelError::InvalidArgument;
  if (const auto error = CreateRamBitmap(args.width, args.height, static_cast<kernel::PixelFormat>(args.format), bitmap))
    return error;

  if (args.pixels && args.pixels.size() > 0) {
    const size_t size = bitmap->sizeInBytes;
    const size_t copySize = args.pixels.size() < size ? args.pixels.size() : size;
    kstd::memcpy(bitmap->backing->directAddress.as_ptr<uint8_t>(), args.pixels.get(), copySize);
  }
  return PublishBitmap(*proc, bitmap, args.handle);
}

// Step 13: the DC's adapter allocates when it can; system RAM otherwise.
kstd::error_code handle_CreateCompatibleBitmap_V1(__KRN_GFX_CREATECOMPATIBLEBITMAP_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  if (args.width == 0 || args.height == 0 || args.width > INT32_MAX || args.height > INT32_MAX)
    return kernel::KernelError::InvalidArgument;

  kernel::PixelFormat format{};
  kernel::KObjectPtr<kernel::KSurface> bitmap;
  {
    // The scene and draw locks keep the engine fill ordered with other draws.
    kernel::user::Guard scene;
    kernel::user::SceneDrawGuard drawing;
    const auto slot = kernel::gfx::FindDC(proc, args.dc);
    if (!slot) return kernel::KernelError::InvalidHandle;
    if (!slot->target) return kernel::KernelError::InvalidState;
    format = slot->target->pixelFormat;
    const auto &adapter = slot->target->adapter;
    if (DrawAdapter(*slot->target) &&
        !kernel::gfx::create_adapter_surface(adapter, args.width, args.height, format, bitmap)) {
      // Adapter memory is not CPU-visible: clear it with the engine, or give it up.
      if (!adapter->FillRect ||
          adapter->FillRect(adapter, bitmap, 0, 0, static_cast<int32_t>(args.width),
                            static_cast<int32_t>(args.height), 0))
        bitmap = {};
    } else {
      bitmap = {};
    }
  }
  if (!bitmap)
    if (const auto error = CreateRamBitmap(args.width, args.height, format, bitmap)) return error;
  return PublishBitmap(*proc, bitmap, args.handle);
}

kstd::error_code handle_DeleteObject_V1(__KRN_GFX_DELETEOBJECT_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  kernel::user::Guard scene;

  const auto surface = ResolveSurface(*proc, args.object);
  if (!surface) return surface.error();

  return proc->handles.Close(static_cast<kernel::KHandle>(args.object));
}

kstd::error_code handle_MoveTo_V1(__KRN_GFX_MOVETO_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  kernel::user::Guard scene;

  const auto slot = kernel::gfx::FindDC(proc, args.dc);
  if (!slot) return kernel::KernelError::InvalidHandle;

  slot->currentX = args.x;
  slot->currentY = args.y;

  return {};
}

kstd::error_code handle_LineTo_V1(__KRN_GFX_LINETO_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  ScreenDrawGuard draw;

  const auto slot = kernel::gfx::FindDC(proc, args.dc);
  if (!slot) return kernel::KernelError::InvalidHandle;


  if (!Drawable(*slot)) {
    return kernel::KernelError::InvalidState;
  }
  const int64_t x0 = static_cast<int64_t>(slot->currentX) + slot->originX;
  const int64_t y0 = static_cast<int64_t>(slot->currentY) + slot->originY;
  const int64_t x1 = static_cast<int64_t>(args.x) + slot->originX;
  const int64_t y1 = static_cast<int64_t>(args.y) + slot->originY;
  {
    const auto rect = DrawBounds(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1,
                                 (x0 < x1 ? x1 : x0) + 1, (y0 < y1 ? y1 : y0) + 1);
    draw.exclude(*slot, &rect);
  }
  kstd::error_code result = kernel::KernelError::Success;
  DirtyRect dirty;
  const auto valid = kernel::gfx::ForEachLinePixel(x0, y0, x1, y1, slot->target->width, slot->target->height,
                                                   [&](int32_t x, int32_t y) {
                                                     if (InClip(*slot, x, y)) {
                                                       const auto error = Fill(*slot, x, y, static_cast<int64_t>(x) + 1,
                                                                               static_cast<int64_t>(y) + 1, slot->penColor, dirty);
                                                       if (!result) result = error;
                                                     }
                                                   });

  slot->currentX = args.x;
  slot->currentY = args.y;
  Present(*slot, dirty);

  return !valid ? result : valid;
}

kstd::error_code handle_Rectangle_V1(__KRN_GFX_RECTANGLE_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  ScreenDrawGuard draw;

  const auto slot = kernel::gfx::FindDC(proc, args.dc);
  if (!slot) return kernel::KernelError::InvalidHandle;


  if (!Drawable(*slot)) {
    return kernel::KernelError::InvalidState;
  }
  {
    const auto rect = DrawBounds(int64_t{args.left} + slot->originX, int64_t{args.top} + slot->originY,
                                 int64_t{args.right} + slot->originX, int64_t{args.bottom} + slot->originY);
    draw.exclude(*slot, &rect);
  }

  DirtyRect dirty;
  const auto result = Fill(*slot, static_cast<int64_t>(args.left) + slot->originX,
                           static_cast<int64_t>(args.top) + slot->originY,
                           static_cast<int64_t>(args.right) + slot->originX,
                           static_cast<int64_t>(args.bottom) + slot->originY, slot->penColor, dirty);
  Present(*slot, dirty);


  return result;
}

kstd::error_code handle_Ellipse_V1(__KRN_GFX_ELLIPSE_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}

kstd::error_code handle_DrawText_V1(__KRN_GFX_DRAWTEXT_V1 &&args) {
  TextEntryState entry;
  const size_t size = args.text.size();
  if (const auto error = ValidateText(args.style, args.layout, args.repertoire,
                                      args.text.data(), size);
      error) return error;
  char text[kernel::font::MaxTextBytes];
  if (size) kstd::memcpy(text, args.text.data(), size);
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  TextRenderState state;
  {
    kernel::user::Guard scene;
    auto *slot = kernel::gfx::FindDC(proc, args.dc);
    if (!slot) return kernel::KernelError::InvalidHandle;
    const auto error = state.capture(*slot);
    if (error) return error;
  }
#if __STDC_HOSTED__
  if (kernel::gfx::test_hooks::drawTextAfterRetain) kernel::gfx::test_hooks::drawTextAfterRetain();
#endif
  auto font = state.font.As<kernel::font::FontObject>();
  kernel::font::TextRun run;
  struct Cleanup {
    kernel::font::TextRun &run;
    TextRenderState &state;
    kernel::KObjectPtr<kernel::font::FontObject> &font;
    ~Cleanup() {
      TextInterruptWindow window;
      run = {};
      state = {};
      font = nullptr;
    }
  } cleanup{run, state, font};
  if (!font) return kernel::KernelError::InvalidState;
  TextMetricsV2 metrics{};
  TextExtentV2 extent{};
  {
    TextInterruptWindow window;
    if (const auto error = PrepareFontText(*font, args.style, args.layout,
                                           args.repertoire, text, size, run,
                                           metrics, extent);
        error)
      return error;
  }
  TextCursor cursor;
  bool done = false;
  while (!done) {
    kstd::error_code error;
    {
      ScreenDrawGuard draw;
      auto *slot = kernel::gfx::FindDC(proc, args.dc);
      if (!slot) {
        error = kernel::KernelError::InvalidHandle;
      } else {
        if (!state.matches(*slot)) {
          error = kernel::KernelError::InvalidState;
        } else {
          draw.exclude(*slot);
          DirtyRect dirty;
          uint32_t processed = 0, runs = 0;
          error = CommitTextSlice(*slot, *font, run, metrics, extent, text, size,
                                  args.layout, int64_t{args.x} + state.originX,
                                  int64_t{args.y} + state.originY, cursor,
                                  done, processed, runs, dirty);
          Present(*slot, dirty);
#if __STDC_HOSTED__
          if (kernel::gfx::test_hooks::textSlice)
            kernel::gfx::test_hooks::textSlice(processed, runs);
#endif
        }
      }
      if (error) {
        if (auto *window = kernel::user::lookup_locked(state.hwnd))
          kernel::user::invalidate_locked(*window, window->painting, false);
      }
    }
    if (error) return error;
    if (!done) { TextInterruptWindow window; }
  }
  return {};
}

kstd::error_code handle_BitBlt_V1(__KRN_GFX_BITBLT_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  ScreenDrawGuard draw;

  const auto dstSlot = kernel::gfx::FindDC(proc, args.dstDC);
  const auto srcSlot = kernel::gfx::FindDC(proc, args.srcDC);
  if (!dstSlot || !srcSlot) return kernel::KernelError::InvalidHandle;

  if (!dstSlot->target || !srcSlot->target) {
    return kernel::KernelError::InvalidArgument;
  }

  // Same-device check: both surfaces belong to one usable adapter; system-RAM surfaces have none.
  const auto &adapter = dstSlot->target->adapter;
  const bool usable = DrawAdapter(*dstSlot->target) && DrawAdapter(*srcSlot->target);
  const bool dstPacked = IsPacked(*dstSlot->target);
  const bool srcPacked = IsPacked(*srcSlot->target);
  // Adapter memory on either side needs the adapter's engine (offscreen <-> screen).
  const bool useHw = usable && adapter->BitBlt && adapter == srcSlot->target->adapter &&
                     (!dstPacked || !srcPacked);
  if (args.rop != 0 || (!useHw && (!dstPacked || !srcPacked))) {
    return kernel::KernelError::InvalidArgument;
  }

  const auto rop = static_cast<kernel::RasterOp>(args.rop);

  kernel::gfx::BlitRect copy{.dstX = static_cast<int64_t>(args.x) + dstSlot->originX,
                             .dstY = static_cast<int64_t>(args.y) + dstSlot->originY,
                             .srcX = static_cast<int64_t>(args.srcX) + srcSlot->originX,
                             .srcY = static_cast<int64_t>(args.srcY) + srcSlot->originY,
                             .width = args.width,
                             .height = args.height};
  if (!kernel::gfx::ClipBlit(copy, dstSlot->target->width, dstSlot->target->height,
                             srcSlot->target->width, srcSlot->target->height)) {
    return {};
  }
  const int32_t dx = static_cast<int32_t>(copy.dstX);
  const int32_t dy = static_cast<int32_t>(copy.dstY);
  const int32_t sx = static_cast<int32_t>(copy.srcX);
  const int32_t sy = static_cast<int32_t>(copy.srcY);
  {
    const auto dstRect = DrawBounds(dx, dy, int64_t{dx} + copy.width, int64_t{dy} + copy.height);
    const auto srcRect = DrawBounds(sx, sy, int64_t{sx} + copy.width, int64_t{sy} + copy.height);
    draw.exclude(*dstSlot, &dstRect);
    draw.exclude(*srcSlot, &srcRect);
  }
  kstd::error_code result = kernel::KernelError::Success;
  // Adapter-memory surfaces have no backing; two null backings are not aliases.
  const bool sharedBacking = dstSlot->target->backing && dstSlot->target->backing == srcSlot->target->backing;
  if (sharedBacking && (dstSlot->target != srcSlot->target)) {
    // Aliased surface views with different pitches/layouts are unsupported.
    result = kernel::KernelError::Unsupported;
  } else if (sharedBacking && (dstSlot->clipped || useHw)) {
    // Global scan order matters even across disjoint clip rectangles. Copy
    // individual pixels in memmove order; drivers never see an overlapping blit.
    const bool reverseY = dy > sy;
    const bool reverseX = dy == sy && dx > sx;
    for (int64_t row = 0; row < copy.height; ++row) {
      const int32_t offY = static_cast<int32_t>(reverseY ? copy.height - 1 - row : row);
      for (int64_t col = 0; col < copy.width; ++col) {
        const int32_t offX = static_cast<int32_t>(reverseX ? copy.width - 1 - col : col);
        if (!InClip(*dstSlot, dx + offX, dy + offY)) continue;
        const auto drawn = !useHw
                               ? KE_GFX_SoftwareBitBlt(dstSlot->target, dx + offX, dy + offY, srcSlot->target, sx + offX, sy + offY, 1, 1, rop)
                               : adapter->BitBlt(adapter, dstSlot->target, dx + offX, dy + offY, srcSlot->target, sx + offX, sy + offY, 1, 1, rop);
        if (!result) result = drawn;
      }
    }
  } else {
    ForEachClip(*dstSlot, dx, dy, dx + copy.width, dy + copy.height,
                [&](int32_t l, int32_t t, int32_t r, int32_t b) {
                  const int32_t csx = sx + (l - dx);
                  const int32_t csy = sy + (t - dy);
                  const auto w = static_cast<uint32_t>(r - l);
                  const auto h = static_cast<uint32_t>(b - t);
                  kstd::error_code drawn = kernel::KernelError::Unsupported;
                  if (useHw) drawn = adapter->BitBlt(adapter, dstSlot->target, l, t, srcSlot->target, csx, csy, w, h, rop);
                  else
                    drawn = KE_GFX_SoftwareBitBlt(dstSlot->target, l, t, srcSlot->target, csx, csy, w, h, rop);
                  if (!result) result = drawn;
                });
  }

  DirtyRect dirty;
  if (!result) dirty.Add(dx, dy, static_cast<int32_t>(dx + copy.width), static_cast<int32_t>(dy + copy.height));
  Present(*dstSlot, dirty);

  return result;
}

kstd::error_code handle_StretchBlt_V1(__KRN_GFX_STRETCHBLT_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}

kstd::error_code handle_AlphaBlend_V1(__KRN_GFX_ALPHABLEND_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}

kstd::error_code handle_CreateRegion_V1(__KRN_GFX_CREATEREGION_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}

kstd::error_code handle_SelectClipRegion_V1(__KRN_GFX_SELECTCLIPREGION_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}

kstd::error_code handle_CombineRegion_V1(__KRN_GFX_COMBINEREGION_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}

kstd::error_code handle_SetViewportOrg_V1(__KRN_GFX_SETVIEWPORTORG_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}

kstd::error_code handle_SetWindowOrg_V1(__KRN_GFX_SETWINDOWORG_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}

kstd::error_code handle_SetDrawColors_V1(__KRN_GFX_SETDRAWCOLORS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  if (args.foreground > 15 || args.background > 15 || args.opaque > 1) return kernel::KernelError::InvalidArgument;
  kernel::user::Guard scene;
  const auto slot = kernel::gfx::FindDC(proc, args.dc);
  if (!slot) return kernel::KernelError::InvalidHandle;
  if (!slot->ChangeRenderState()) {
    return kernel::KernelError::InvalidState;
  }
  slot->penColor = args.foreground;
  slot->backgroundColor = args.background;
  slot->opaqueText = args.opaque != 0;
  return {};
}

kstd::error_code handle_GetBitmapBits_V1(__KRN_GFX_GETBITMAPBITS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  kernel::user::Guard scene;

  const auto resolved = ResolveSurface(*proc, args.bitmap);
  if (!resolved) return resolved.error();
  const auto &bitmap = *resolved;
  if (!bitmap->backing || !bitmap->backing->directAddress) return kernel::KernelError::InvalidState;
  if (!args.buffer) return kernel::KernelError::InvalidArgument;

  const size_t surfaceSize = SurfaceSizeInBytes(*bitmap);
  const size_t copySize = args.buffer.size() < surfaceSize ? args.buffer.size() : surfaceSize;

  kstd::memcpy(args.buffer.get(), bitmap->backing->directAddress.as_ptr<uint8_t>(), copySize);
  args.buffer_len = copySize;

  return {};
}

kstd::error_code handle_SetBitmapBits_V1(__KRN_GFX_SETBITMAPBITS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  kernel::user::Guard scene;

  const auto resolved = ResolveSurface(*proc, args.bitmap);
  if (!resolved) return resolved.error();
  const auto &bitmap = *resolved;
  if (!bitmap->backing || !bitmap->backing->directAddress) return kernel::KernelError::InvalidState;
  if (!args.buffer) return kernel::KernelError::InvalidArgument;

  const size_t surfaceSize = SurfaceSizeInBytes(*bitmap);
  const size_t copySize = args.buffer.size() < surfaceSize ? args.buffer.size() : surfaceSize;

  kstd::memcpy(bitmap->backing->directAddress.as_ptr<uint8_t>(), args.buffer.get(), copySize);

  return {};
}

kstd::error_code handle_GetDeviceCaps_V1(__KRN_GFX_GETDEVICECAPS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  kernel::user::Guard scene;

  const auto slot = kernel::gfx::FindDC(proc, args.dc);
  if (!slot) return kernel::KernelError::InvalidHandle;


  if (!slot->target) {
    return kernel::KernelError::InvalidState;
  }

  args.caps.width = slot->target->width;
  args.caps.height = slot->target->height;
  args.caps.colorDepth = slot->target->bpp ? slot->target->bpp : BytesPerPixel(slot->target->pixelFormat) * 8;
  args.caps.accelFlags = 0;


  return {};
}
