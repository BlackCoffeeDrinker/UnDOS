#include "stdkrn.hpp"
#include "syscalls.hpp"
#include "user/user.hpp"
#include "gfx/gfx.hpp"
#include <kernel/gfx/bitmap_font.hpp>
#include <kernel/gfx/raster_helpers.hpp>
#include <undos/font/resource.hpp>

// GFX (GDI-style) syscall handlers (300-399), generated from
// tools/gfxsyscall.txt (pulled into tools/syscalls.txt via `include`).
//
// Palette16 drawing state, fixed bitmap text and SRCCOPY are the minimal shell
// contract. Unused object/region/stretch/alpha APIs remain explicitly unsupported.
//
// Device contexts (DCs) are ephemeral per-process state, not Object-Manager
// resources: each KProcessObject owns its own DeviceContextTable
// (proc->dcTable), resolved via a generation-tagged HDC handle rather than
// the generic ProcessHandleTable used for long-lived KObjects like KSurface.

namespace {

kernel::font::ResourceBudget fontResourceBudget;

SyscallReturnCode FontResult(kernel::font::Result result) {
  using kernel::font::Result;
  switch (result) {
    case Result::Okay: return SyscallReturnCode_OKAY;
    case Result::OutOfMemory: return SyscallReturnCode_OUT_OF_MEMORY;
    case Result::InvalidSize: return SyscallReturnCode_INVALID_SIZE;
    case Result::Limit: return SyscallReturnCode_OUT_OF_MEMORY;
    default: return SyscallReturnCode_INVALID_ARGUMENT;
  }
}

struct OwnedBitmap final : kernel::KSurface {
  using KSurface::KSurface;
  void *pixels = nullptr;
  ~OwnedBitmap() override {
    if (pixels) KE_VMM_FreeRegion(KE_VMM_GetKernelAddressSpace(), pixels);
  }
};

// USER's scene lock serializes screen writes with cursor save/restore and
// nonclient repaint. It must be taken before any per-DC lock.
struct ScreenDrawGuard {
  kernel::user::Guard scene;
  ScreenDrawGuard() { kernel::user::cursor_hide_locked(); }
  ~ScreenDrawGuard() { kernel::user::cursor_show_locked(); }
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

// Resolves a HANDLER value to a live KSurface (bitmap or DC target), or null.
kernel::KObjectPtr<kernel::KSurface> ResolveSurface(kernel::KProcessObject &proc, uint32_t handle) {
  const auto entry = proc.handles.Lookup(static_cast<kernel::KHandle>(handle));
  if (!entry) return nullptr;
  return entry->object.As<kernel::KSurface>();
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

bool IsPacked(const kernel::KSurface &surface) {
  return surface.type == kernel::SurfaceType::OffscreenBitmap ||
         (surface.flags & kernel::SurfaceFlags::Linear) != 0;
}

uint32_t NativeColor(const kernel::KSurface &surface, uint32_t index) {
  const auto rgb = kernel::gfx::Palette16Rgb(index);
  switch (surface.pixelFormat) {
    case kernel::PixelFormat::INDEX8:
      return surface.type == kernel::SurfaceType::Framebuffer && IsPacked(surface)
                 ? kernel::gfx::RgbTo332(rgb) : index;
    case kernel::PixelFormat::RGB15: return kernel::gfx::RgbTo555(rgb);
    case kernel::PixelFormat::RGB16: return kernel::gfx::RgbTo565(rgb);
    case kernel::PixelFormat::ARGB32: return 0xff000000u | rgb;
    case kernel::PixelFormat::RGB24: return rgb;
  }
  return 0;
}

struct DirtyRect {
  kernel::KClipRect rect{INT32_MAX, INT32_MAX, 0, 0};
  void Add(int32_t l, int32_t t, int32_t r, int32_t b) {
    if (l < rect.left) rect.left = l;
    if (t < rect.top) rect.top = t;
    if (r > rect.right) rect.right = r;
    if (b > rect.bottom) rect.bottom = b;
  }
};

bool Fill(kernel::KDeviceContextSlot &slot, int64_t l, int64_t t, int64_t r, int64_t b,
          uint32_t color, DirtyRect &dirty) {
  if (!slot.target || !slot.target->backing) return false;
  bool ok = true;
  const auto native = NativeColor(*slot.target, color);
  ForEachClip(slot, l, t, r, b, [&](int32_t cl, int32_t ct, int32_t cr, int32_t cb) {
    bool drawn = false;
    const auto adapter = slot.display ? slot.display->adapter : nullptr;
    const bool screen = slot.display && slot.target == slot.display->surface;
    const bool packed = IsPacked(*slot.target);
    const bool tiny = static_cast<int64_t>(cr - cl) * (cb - ct) <= 256;
    if (packed && (tiny || slot.noAccel))
      drawn = KE_GFX_SoftwareFillRect(slot.target, cl, ct, cr - cl, cb - ct, native);
    if (!drawn && screen && adapter && adapter->FillRect && (!slot.noAccel || !packed))
      drawn = adapter->FillRect(adapter, slot.target, cl, ct, cr - cl, cb - ct, native);
    if (!drawn && packed)
      drawn = KE_GFX_SoftwareFillRect(slot.target, cl, ct, cr - cl, cb - ct, native);
    if (drawn) dirty.Add(cl, ct, cr, cb);
    ok = ok && drawn;
  });
  return ok;
}

void Present(kernel::KDeviceContextSlot &slot, const DirtyRect &dirty) {
  // BeginPaint DCs are flushed once by USER EndPaint, not once per icon span
  // or text label. Unrestricted screen DCs still present synchronously.
  if (slot.clipped && slot.clipOwner) {
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
      slot.display && slot.target == slot.display->surface && slot.display->adapter && slot.display->adapter->Present)
    slot.display->adapter->Present(slot.display->adapter, slot.target, &dirty.rect, 1);
}

}// namespace

SyscallReturnCode handle_QueryFontEngine_V1(__KRN_GFX_QUERYFONTENGINE_V1 &&args) {
  args.version = undos::font::Version;
  args.capabilities = 1;
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_BeginFontUpload_V1(__KRN_GFX_BEGINFONTUPLOAD_V1 &&args) {
  args.upload = 0;
  auto *process = CurrentProcess();
  if (!process) return SyscallReturnCode_INVALID_ARGUMENT;
  return FontResult(process->fontResources.begin(fontResourceBudget, args.totalBytes, args.upload));
}

SyscallReturnCode handle_AppendFontUpload_V1(__KRN_GFX_APPENDFONTUPLOAD_V1 &&args) {
  auto *process = CurrentProcess();
  if (!process) return SyscallReturnCode_INVALID_ARGUMENT;
  return FontResult(process->fontResources.append(args.upload, args.offset, args.data.get(), args.data.size()));
}

SyscallReturnCode handle_CommitFontUpload_V1(__KRN_GFX_COMMITFONTUPLOAD_V1 &&args) {
  args.face = 0;
  args.validationError = 0;
  auto *process = CurrentProcess();
  if (!process) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto result = process->fontResources.commit(args.upload, args.face, args.validationError);
  if (result == kernel::font::Result::InvalidResource)
    KE_Print("GFX Font Engine: registration rejected: {}",
             undos::font::error_message(static_cast<undos::font::Error>(args.validationError)));
  return FontResult(result);
}

SyscallReturnCode handle_CloseFontResource_V1(__KRN_GFX_CLOSEFONTRESOURCE_V1 &&args) {
  auto *process = CurrentProcess();
  if (!process) return SyscallReturnCode_INVALID_ARGUMENT;
  return FontResult(process->fontResources.close(args.handle));
}

SyscallReturnCode handle_CreateDC_V1(__KRN_GFX_CREATEDC_V1 &&args) {
  args.handle = 0;
  (void) args.driverName;
  (void) args.deviceName;
  const auto flags = static_cast<uint32_t>(args.initFlags);
  constexpr uint32_t supported = DCInitFlags_Primary | DCInitFlags_Memory | DCInitFlags_Offscreen | DCInitFlags_NoAccel;
  const bool memory = (flags & (DCInitFlags_Memory | DCInitFlags_Offscreen)) != 0;
  if ((flags & ~supported) || (memory && (flags & DCInitFlags_Primary))) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::user::Guard scene;

  const auto display = KE_GFX_GetPrimaryDisplay();
  if (!memory && (!display || !display->surface || !display->isAttachedToDesktop))
    return SyscallReturnCode_UNSUPPORTED;

  auto [slot, hdc] = proc->dcTable.Allocate();
  if (!slot) return SyscallReturnCode_OUT_OF_MEMORY;

  slot->lock.lock();
  slot->display = display;
  slot->memoryDC = memory;
  slot->noAccel = (flags & DCInitFlags_NoAccel) != 0;
  slot->target = memory ? nullptr : display->surface;
  slot->lock.unlock();

  args.handle = hdc;
  return SyscallReturnCode_OKAY;
}

namespace {
DisplayMode PublicMode(const kernel::KDisplayModeInfo &mode) {
  return {static_cast<uint32_t>(mode.modeIndex), mode.width, mode.height, mode.bpp, mode.refreshRate};
}
}

SyscallReturnCode handle_EnumDisplayModes_V1(__KRN_GFX_ENUMDISPLAYMODES_V1 &&args) {
  args.mode = {};
  args.available = 0;
  kernel::user::Guard scene;
  const auto display = KE_GFX_GetPrimaryDisplay();
  if (!display || !display->GetModeInfo || !display->SetMode) return SyscallReturnCode_UNSUPPORTED;
  kernel::KDisplayModeInfo mode{};
  if (kernel::gfx::enumerate_mode(display, args.index, mode)) {
    args.mode = PublicMode(mode);
    args.available = 1;
  }
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_GetDisplayMode_V1(__KRN_GFX_GETDISPLAYMODE_V1 &&args) {
  args.state = {};
  kernel::user::Guard scene;
  const auto display = KE_GFX_GetPrimaryDisplay();
  if (!display) return SyscallReturnCode_UNSUPPORTED;
  args.state.active = display->isAttachedToDesktop ? 1 : 0;
  args.state.generation = display->modeGeneration;
  args.state.mode = PublicMode(display->currentMode);
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_SetDisplayMode_V1(__KRN_GFX_SETDISPLAYMODE_V1 &&args) {
  kernel::user::Guard scene;
  const auto display = KE_GFX_GetPrimaryDisplay();
  if (!display || !display->GetModeInfo || !display->SetMode) return SyscallReturnCode_UNSUPPORTED;
  kernel::KDisplayModeInfo mode{};
  if (!display->GetModeInfo(display, args.modeId, mode) ||
      !kernel::gfx::usable_mode(display, mode)) return SyscallReturnCode_INVALID_ARGUMENT;
  if (display->isAttachedToDesktop && display->currentMode.modeIndex == args.modeId)
    return SyscallReturnCode_OKAY;
  kernel::user::SceneDrawGuard drawing;
  const auto generation = display->modeGeneration;
  const bool changed = KE_GFX_SetDisplayMode(display, args.modeId);
  if (generation != display->modeGeneration) {
    kernel::user::cursor_reset_locked();
    kernel::user::display_input_changed_locked(generation == 0);
    kernel::user::display_changed_locked(display->isAttachedToDesktop ? mode.bpp : 0);
  }
  return changed ? SyscallReturnCode_OKAY : SyscallReturnCode_UNKNOWN;
}

SyscallReturnCode handle_DeleteDC_V1(__KRN_GFX_DELETEDC_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::user::Guard scene;

  if (proc->dcTable.Close(args.dc)) {
    return SyscallReturnCode_OKAY;
  }
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_SelectObject_V1(__KRN_GFX_SELECTOBJECT_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::user::Guard scene;

  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto bitmap = ResolveSurface(*proc, args.object);
  if (!bitmap) return SyscallReturnCode_INVALID_ARGUMENT;

  slot->lock.lock();
  if (!slot->memoryDC) {
    slot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }
  slot->selectedBitmap = bitmap;
  slot->target = bitmap;
  slot->lock.unlock();

  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_CreatePen_V1(__KRN_GFX_CREATEPEN_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_CreateBrush_V1(__KRN_GFX_CREATEBRUSH_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_CreateFont_V1(__KRN_GFX_CREATEFONT_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_CreateBitmap_V1(__KRN_GFX_CREATEBITMAP_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  if (args.width == 0 || args.height == 0 || args.width > INT32_MAX || args.height > INT32_MAX ||
      static_cast<uint32_t>(args.format) > 2) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto pixelFormat = static_cast<kernel::PixelFormat>(args.format);
  const uint32_t bpp = BytesPerPixel(pixelFormat);
  const uint64_t widePitch = static_cast<uint64_t>(args.width) * bpp;
  const uint64_t wideSize = widePitch * args.height;
  // A bounded allocation protects the 32-bit kernel and low-memory targets.
  if (widePitch > INT32_MAX || wideSize > 16u * 1024u * 1024u) return SyscallReturnCode_INVALID_ARGUMENT;
  const size_t pitch = static_cast<size_t>(widePitch);
  const size_t size = static_cast<size_t>(wideSize);

  auto bitmap = kernel::CreateKObject<OwnedBitmap>("Bitmap");
  if (!bitmap) return SyscallReturnCode_OUT_OF_MEMORY;
  auto backing = kernel::CreateKObject<kernel::KMemoryBackingObject>("BitmapBacking");
  if (!backing) return SyscallReturnCode_OUT_OF_MEMORY;

  auto &as = KE_VMM_GetKernelAddressSpace();
  void *region = KE_VMM_AllocateRegion(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!region) return SyscallReturnCode_OUT_OF_MEMORY;

  bitmap->pixels = region;
  backing->directAddress = kernel::VirtualAddress::from_ptr(region);
  backing->size = size;
  bitmap->backing = backing;
  bitmap->width = args.width;
  bitmap->height = args.height;
  bitmap->pitch = static_cast<int32_t>(pitch);
  bitmap->pixelFormat = pixelFormat;
  bitmap->bpp = bpp * 8;
  bitmap->sizeInBytes = size;
  bitmap->flags = kernel::SurfaceFlags::TopDown | kernel::SurfaceFlags::Linear;
  __builtin_memset(region, 0, size);

  if (args.pixels && args.pixels.size() > 0) {
    const size_t copySize = args.pixels.size() < size ? args.pixels.size() : size;
    kstd::memcpy(backing->directAddress.as_ptr<uint8_t>(), args.pixels.get(), copySize);
  }

  kernel::user::Guard scene;
  const kernel::KHandle h = proc->handles.Allocate(
      bitmap,
      kernel::ProcessHandleTable::HandleAccess::Read |
          kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::None);

  args.handle = static_cast<uint32_t>(h);
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_DeleteObject_V1(__KRN_GFX_DELETEOBJECT_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::user::Guard scene;

  if (!ResolveSurface(*proc, args.object)) return SyscallReturnCode_INVALID_ARGUMENT;

  if (proc->handles.Close(static_cast<kernel::KHandle>(args.object))) {
    return SyscallReturnCode_OKAY;
  }
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_MoveTo_V1(__KRN_GFX_MOVETO_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::user::Guard scene;

  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;

  slot->lock.lock();
  slot->currentX = args.x;
  slot->currentY = args.y;
  slot->lock.unlock();

  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_LineTo_V1(__KRN_GFX_LINETO_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  ScreenDrawGuard draw;

  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;

  slot->lock.lock();

  if (!slot->target || !slot->target->backing) {
    slot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  const int64_t x0 = static_cast<int64_t>(slot->currentX) + slot->originX;
  const int64_t y0 = static_cast<int64_t>(slot->currentY) + slot->originY;
  const int64_t x1 = static_cast<int64_t>(args.x) + slot->originX;
  const int64_t y1 = static_cast<int64_t>(args.y) + slot->originY;
  bool ok = true;
  DirtyRect dirty;
  const bool valid = kernel::gfx::ForEachLinePixel(x0, y0, x1, y1, slot->target->width, slot->target->height,
      [&](int32_t x, int32_t y) {
        if (InClip(*slot, x, y)) ok = Fill(*slot, x, y, static_cast<int64_t>(x) + 1,
                                         static_cast<int64_t>(y) + 1, slot->penColor, dirty) && ok;
      });

  slot->currentX = args.x;
  slot->currentY = args.y;
  Present(*slot, dirty);
  slot->lock.unlock();

  return ok && valid ? SyscallReturnCode_OKAY : SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_Rectangle_V1(__KRN_GFX_RECTANGLE_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  ScreenDrawGuard draw;

  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;

  slot->lock.lock();

  if (!slot->target || !slot->target->backing) {
    slot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  DirtyRect dirty;
  const bool ok = Fill(*slot, static_cast<int64_t>(args.left) + slot->originX,
                       static_cast<int64_t>(args.top) + slot->originY,
                       static_cast<int64_t>(args.right) + slot->originX,
                       static_cast<int64_t>(args.bottom) + slot->originY, slot->penColor, dirty);
  Present(*slot, dirty);

  slot->lock.unlock();

  return ok ? SyscallReturnCode_OKAY : SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_Ellipse_V1(__KRN_GFX_ELLIPSE_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_TextOut_V1(__KRN_GFX_TEXTOUT_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  ScreenDrawGuard draw;
  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;
  slot->lock.lock();
  if (!slot->target || !slot->target->backing) {
    slot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }
  bool ok = true;
  DirtyRect dirty;
  kernel::gfx::DrawBitmapText(static_cast<int64_t>(args.x) + slot->originX,
                              static_cast<int64_t>(args.y) + slot->originY,
                              args.text.data(), args.text.size(), slot->penColor,
                              slot->backgroundColor, slot->opaqueText,
                              [&](int64_t l, int64_t t, int64_t r, int64_t b, uint32_t color) {
                                ok = Fill(*slot, l, t, r, b, color, dirty) && ok;
                              });
  Present(*slot, dirty);
  slot->lock.unlock();
  return ok ? SyscallReturnCode_OKAY : SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_BitBlt_V1(__KRN_GFX_BITBLT_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  ScreenDrawGuard draw;

  const auto dstSlot = proc->dcTable.Resolve(args.dstDC);
  const auto srcSlot = proc->dcTable.Resolve(args.srcDC);
  if (!dstSlot || !srcSlot) return SyscallReturnCode_INVALID_ARGUMENT;

  // A single HDC can be resolved for both dst and src (e.g. blitting within
  // the same surface); SpinLock is not recursive, so only lock once in that
  // case to avoid a self-deadlock.
  const bool sameSlot = dstSlot == srcSlot;

  // Consistent ordering avoids AB/BA deadlocks between two process threads.
  auto *first = args.dstDC < args.srcDC ? dstSlot : srcSlot;
  auto *second = first == dstSlot ? srcSlot : dstSlot;
  first->lock.lock();
  if (!sameSlot) second->lock.lock();

  if (!dstSlot->target || !srcSlot->target) {
    if (!sameSlot) srcSlot->lock.unlock();
    dstSlot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  const auto adapter = dstSlot->display ? dstSlot->display->adapter : nullptr;
  const bool dstPacked = IsPacked(*dstSlot->target);
  const bool srcPacked = IsPacked(*srcSlot->target);
  if (args.rop != 0 || dstPacked != srcPacked ||
      (!dstPacked && (!adapter || !adapter->BitBlt || dstSlot->display != srcSlot->display))) {
    if (!sameSlot) srcSlot->lock.unlock();
    dstSlot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  const auto rop = static_cast<kernel::RasterOp>(args.rop);

  kernel::gfx::BlitRect copy{static_cast<int64_t>(args.x) + dstSlot->originX,
                            static_cast<int64_t>(args.y) + dstSlot->originY,
                            static_cast<int64_t>(args.srcX) + srcSlot->originX,
                            static_cast<int64_t>(args.srcY) + srcSlot->originY,
                            args.width, args.height};
  if (!kernel::gfx::ClipBlit(copy, dstSlot->target->width, dstSlot->target->height,
                             srcSlot->target->width, srcSlot->target->height)) {
    if (!sameSlot) srcSlot->lock.unlock();
    dstSlot->lock.unlock();
    return SyscallReturnCode_OKAY;
  }
  const int32_t dx = static_cast<int32_t>(copy.dstX);
  const int32_t dy = static_cast<int32_t>(copy.dstY);
  const int32_t sx = static_cast<int32_t>(copy.srcX);
  const int32_t sy = static_cast<int32_t>(copy.srcY);
  bool ok = true;
  const bool sharedBacking = dstSlot->target->backing == srcSlot->target->backing;
  if (sharedBacking && (dstSlot->target != srcSlot->target)) {
    // Aliased surface views with different pitches/layouts are unsupported.
    ok = false;
  } else if (sharedBacking && (dstSlot->clipped || !dstPacked)) {
    // Global scan order matters even across disjoint clip rectangles. Copy
    // individual pixels in memmove order; drivers never see an overlapping blit.
    const bool reverseY = dy > sy;
    const bool reverseX = dy == sy && dx > sx;
    for (int64_t row = 0; row < copy.height; ++row) {
      const int32_t offY = static_cast<int32_t>(reverseY ? copy.height - 1 - row : row);
      for (int64_t col = 0; col < copy.width; ++col) {
        const int32_t offX = static_cast<int32_t>(reverseX ? copy.width - 1 - col : col);
        if (!InClip(*dstSlot, dx + offX, dy + offY)) continue;
        const bool drawn = dstPacked
            ? KE_GFX_SoftwareBitBlt(dstSlot->target, dx + offX, dy + offY, srcSlot->target, sx + offX, sy + offY, 1, 1, rop)
            : adapter->BitBlt(adapter, dstSlot->target, dx + offX, dy + offY, srcSlot->target, sx + offX, sy + offY, 1, 1, rop);
        ok = drawn && ok;
      }
    }
  } else {
    ForEachClip(*dstSlot, dx, dy, dx + copy.width, dy + copy.height,
              [&](int32_t l, int32_t t, int32_t r, int32_t b) {
                const int32_t csx = sx + (l - dx);
                const int32_t csy = sy + (t - dy);
                const auto w = static_cast<uint32_t>(r - l);
                const auto h = static_cast<uint32_t>(b - t);
                bool drawn = false;
                if (!dstPacked) drawn = adapter->BitBlt(adapter, dstSlot->target, l, t, srcSlot->target, csx, csy, w, h, rop);
                else drawn = KE_GFX_SoftwareBitBlt(dstSlot->target, l, t, srcSlot->target, csx, csy, w, h, rop);
                ok = ok && drawn;
              });
  }

  DirtyRect dirty;
  if (ok) dirty.Add(dx, dy, static_cast<int32_t>(dx + copy.width), static_cast<int32_t>(dy + copy.height));
  Present(*dstSlot, dirty);
  if (!sameSlot) srcSlot->lock.unlock();
  dstSlot->lock.unlock();

  return ok ? SyscallReturnCode_OKAY : SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_StretchBlt_V1(__KRN_GFX_STRETCHBLT_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_AlphaBlend_V1(__KRN_GFX_ALPHABLEND_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_CreateRegion_V1(__KRN_GFX_CREATEREGION_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_SelectClipRegion_V1(__KRN_GFX_SELECTCLIPREGION_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_CombineRegion_V1(__KRN_GFX_COMBINEREGION_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_SetViewportOrg_V1(__KRN_GFX_SETVIEWPORTORG_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_SetWindowOrg_V1(__KRN_GFX_SETWINDOWORG_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_GetTextMetrics_V1(__KRN_GFX_GETTEXTMETRICS_V1 &&args) {
  const auto proc = CurrentProcess();
  kernel::user::Guard scene;
  if (!proc || !proc->dcTable.Resolve(args.dc)) return SyscallReturnCode_INVALID_ARGUMENT;
  args.metrics.cellWidth = kernel::gfx::kFontCellWidth;
  args.metrics.cellHeight = kernel::gfx::kFontCellHeight;
  args.metrics.ascent = kernel::gfx::kFontAscent;
  args.metrics.descent = kernel::gfx::kFontDescent;
  args.metrics.firstChar = kernel::gfx::kFontFirstChar;
  args.metrics.lastChar = kernel::gfx::kFontLastChar;
  args.metrics.defaultChar = kernel::gfx::kFontDefaultChar;
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_SetDrawColors_V1(__KRN_GFX_SETDRAWCOLORS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc || args.foreground > 15 || args.background > 15 || args.opaque > 1) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::user::Guard scene;
  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;
  slot->lock.lock();
  slot->penColor = args.foreground;
  slot->backgroundColor = args.background;
  slot->opaqueText = args.opaque != 0;
  slot->lock.unlock();
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_GetBitmapBits_V1(__KRN_GFX_GETBITMAPBITS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::user::Guard scene;

  const auto bitmap = ResolveSurface(*proc, args.bitmap);
  if (!bitmap || !bitmap->backing || !bitmap->backing->directAddress) return SyscallReturnCode_INVALID_ARGUMENT;
  if (!args.buffer) return SyscallReturnCode_INVALID_ARGUMENT;

  const size_t surfaceSize = SurfaceSizeInBytes(*bitmap);
  const size_t copySize = args.buffer.size() < surfaceSize ? args.buffer.size() : surfaceSize;

  kstd::memcpy(args.buffer.get(), bitmap->backing->directAddress.as_ptr<uint8_t>(), copySize);
  args.buffer_len = copySize;

  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_SetBitmapBits_V1(__KRN_GFX_SETBITMAPBITS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::user::Guard scene;

  const auto bitmap = ResolveSurface(*proc, args.bitmap);
  if (!bitmap || !bitmap->backing || !bitmap->backing->directAddress) return SyscallReturnCode_INVALID_ARGUMENT;
  if (!args.buffer) return SyscallReturnCode_INVALID_ARGUMENT;

  const size_t surfaceSize = SurfaceSizeInBytes(*bitmap);
  const size_t copySize = args.buffer.size() < surfaceSize ? args.buffer.size() : surfaceSize;

  kstd::memcpy(bitmap->backing->directAddress.as_ptr<uint8_t>(), args.buffer.get(), copySize);

  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_GetDeviceCaps_V1(__KRN_GFX_GETDEVICECAPS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::user::Guard scene;

  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;

  slot->lock.lock();

  if (!slot->target) {
    slot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  args.caps.width = slot->target->width;
  args.caps.height = slot->target->height;
  args.caps.colorDepth = slot->target->bpp ? slot->target->bpp : BytesPerPixel(slot->target->pixelFormat) * 8;
  args.caps.accelFlags = 0;

  slot->lock.unlock();

  return SyscallReturnCode_OKAY;
}
