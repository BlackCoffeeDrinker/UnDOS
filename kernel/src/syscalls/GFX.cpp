#include "stdkrn.hpp"
#include "syscalls.hpp"

// GFX (GDI-style) syscall handlers (300-399), generated from
// tools/gfxsyscall.txt (pulled into tools/syscalls.txt via `include`).
//
// The driver only ever exposes a single, fixed 640x480x16 surface (see
// drivers/gfx_vga), so only the primitives that map directly onto the
// KGraphicsAdapter/KDisplay/KSurface cfunc hooks are implemented here; the
// rest (fonts, regions, alpha blend, stretch blit, viewport/window origin)
// are explicitly out of scope and stubbed as SyscallReturnCode_INVALID_ARGUMENT.
//
// Device contexts (DCs) are ephemeral per-process state, not Object-Manager
// resources: each KProcessObject owns its own DeviceContextTable
// (proc->dcTable), resolved via a generation-tagged HDC handle rather than
// the generic ProcessHandleTable used for long-lived KObjects like KSurface.

namespace {

size_t BytesPerPixel(kernel::PixelFormat format) {
  switch (format) {
    case kernel::PixelFormat::RGB24:
      return 3;
    case kernel::PixelFormat::ARGB32:
      return 4;
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
void ForEachClip(const kernel::KDeviceContextSlot &slot, int32_t l, int32_t t, int32_t r, int32_t b, Fn &&fn) {
  if (!slot.clipped) {
    if (r > l && b > t) fn(l, t, r, b);
    return;
  }
  for (uint32_t i = 0; i < slot.clipCount; ++i) {
    const auto &c = slot.clipRects[i];
    const int32_t cl = l > c.left ? l : c.left;
    const int32_t ct = t > c.top ? t : c.top;
    const int32_t cr = r < c.right ? r : c.right;
    const int32_t cb = b < c.bottom ? b : c.bottom;
    if (cr > cl && cb > ct) fn(cl, ct, cr, cb);
  }
}

bool InClip(const kernel::KDeviceContextSlot &slot, int32_t x, int32_t y) {
  if (!slot.clipped) return true;
  for (uint32_t i = 0; i < slot.clipCount; ++i) {
    const auto &c = slot.clipRects[i];
    if (x >= c.left && x < c.right && y >= c.top && y < c.bottom) return true;
  }
  return false;
}

}// namespace

SyscallReturnCode handle_CreateDC_V1(__KRN_GFX_CREATEDC_V1 &&args) {
  (void) args.driverName;
  (void) args.deviceName;
  (void) args.initFlags;

  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto display = KE_GFX_GetPrimaryDisplay();
  if (!display) return SyscallReturnCode_INVALID_ARGUMENT;

  if (!display->surface) {
    display->surface = KE_GFX_CreateSurfaceForMode(display, 0);
    if (!display->surface) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  auto [slot, hdc] = proc->dcTable.Allocate();
  if (!slot) return SyscallReturnCode_OUT_OF_MEMORY;

  slot->lock.lock();
  slot->display = display;
  slot->target = display->surface;
  slot->lock.unlock();

  args.handle = hdc;
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_DeleteDC_V1(__KRN_GFX_DELETEDC_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  if (proc->dcTable.Close(args.dc)) {
    return SyscallReturnCode_OKAY;
  }
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_SelectObject_V1(__KRN_GFX_SELECTOBJECT_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto bitmap = ResolveSurface(*proc, args.object);
  if (!bitmap) return SyscallReturnCode_INVALID_ARGUMENT;

  slot->lock.lock();
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

  if (args.width == 0 || args.height == 0) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto pixelFormat = static_cast<kernel::PixelFormat>(args.format);
  const size_t bpp = BytesPerPixel(pixelFormat);
  const size_t pitch = args.width * bpp;
  const size_t size = pitch * args.height;

  auto &as = KE_VMM_GetKernelAddressSpace();
  void *region = KE_VMM_AllocateRegion(as, size, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!region) return SyscallReturnCode_OUT_OF_MEMORY;

  auto backing = KE_VMM_CreateDirectBacking(kernel::VirtualAddress::from_ptr(region), size);
  if (!backing) return SyscallReturnCode_OUT_OF_MEMORY;

  auto bitmap = kernel::CreateKObject<kernel::KSurface>("Bitmap");
  if (!bitmap) return SyscallReturnCode_OUT_OF_MEMORY;

  bitmap->backing = backing;
  bitmap->width = args.width;
  bitmap->height = args.height;
  bitmap->pitch = static_cast<uint32_t>(pitch);
  bitmap->pixelFormat = pixelFormat;

  if (args.pixels && args.pixels.size() > 0) {
    const size_t copySize = args.pixels.size() < size ? args.pixels.size() : size;
    kstd::memcpy(backing->directAddress.as_ptr<uint8_t>(), args.pixels.get(), copySize);
  }

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

  if (!ResolveSurface(*proc, args.object)) return SyscallReturnCode_INVALID_ARGUMENT;

  if (proc->handles.Close(static_cast<kernel::KHandle>(args.object))) {
    return SyscallReturnCode_OKAY;
  }
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_MoveTo_V1(__KRN_GFX_MOVETO_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

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

  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;

  slot->lock.lock();

  if (!slot->target || !slot->target->backing) {
    slot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  const auto adapter = slot->display ? slot->display->adapter : nullptr;
  if (!adapter || !adapter->DrawLine) {
    slot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  const int32_t x0 = slot->currentX + slot->originX;
  const int32_t y0 = slot->currentY + slot->originY;
  const int32_t x1 = args.x + slot->originX;
  const int32_t y1 = args.y + slot->originY;
  bool ok = true;
  if (slot->clipped) {
    // Clipped DCs plot the line point by point so each pixel is tested.
    int32_t x = x0, y = y0;
    const int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
      if (InClip(*slot, x, y)) KE_GFX_SoftwareFillRect(slot->target, x, y, 1, 1, slot->penColor);
      if (x == x1 && y == y1) break;
      const int32_t e2 = 2 * err;
      if (e2 >= dy) { err += dy; x += sx; }
      if (e2 <= dx) { err += dx; y += sy; }
    }
  } else {
    ok = adapter->DrawLine(slot->target, x0, y0, x1, y1, slot->penColor);
    if (!ok) {
      ok = KE_GFX_SoftwareDrawLine(slot->target, x0, y0, x1, y1, slot->penColor);
    }
  }

  slot->currentX = args.x;
  slot->currentY = args.y;
  slot->lock.unlock();

  return ok ? SyscallReturnCode_OKAY : SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_Rectangle_V1(__KRN_GFX_RECTANGLE_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;

  slot->lock.lock();

  if (!slot->target || !slot->target->backing) {
    slot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  const auto adapter = slot->display ? slot->display->adapter : nullptr;
  if (!adapter) {
    slot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  bool ok = true;
  ForEachClip(*slot, args.left + slot->originX, args.top + slot->originY,
              args.right + slot->originX, args.bottom + slot->originY,
              [&](int32_t l, int32_t t, int32_t r, int32_t b) {
                bool drawn = adapter->FillRect && adapter->FillRect(slot->target, l, t, r - l, b - t, slot->penColor);
                if (!drawn) drawn = KE_GFX_SoftwareFillRect(slot->target, l, t, r - l, b - t, slot->penColor);
                ok = ok && drawn;
              });

  slot->lock.unlock();

  return ok ? SyscallReturnCode_OKAY : SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_Ellipse_V1(__KRN_GFX_ELLIPSE_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_TextOut_V1(__KRN_GFX_TEXTOUT_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_BitBlt_V1(__KRN_GFX_BITBLT_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto dstSlot = proc->dcTable.Resolve(args.dstDC);
  const auto srcSlot = proc->dcTable.Resolve(args.srcDC);
  if (!dstSlot || !srcSlot) return SyscallReturnCode_INVALID_ARGUMENT;

  // A single HDC can be resolved for both dst and src (e.g. blitting within
  // the same surface); SpinLock is not recursive, so only lock once in that
  // case to avoid a self-deadlock.
  const bool sameSlot = dstSlot == srcSlot;

  dstSlot->lock.lock();
  if (!sameSlot) srcSlot->lock.lock();

  if (!dstSlot->target || !srcSlot->target) {
    if (!sameSlot) srcSlot->lock.unlock();
    dstSlot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  const auto adapter = dstSlot->display ? dstSlot->display->adapter : nullptr;
  if (!adapter) {
    if (!sameSlot) srcSlot->lock.unlock();
    dstSlot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  const auto rop = static_cast<kernel::RasterOp>(args.rop);

  const int32_t dx = args.x + dstSlot->originX;
  const int32_t dy = args.y + dstSlot->originY;
  const int32_t sx = args.srcX + srcSlot->originX;
  const int32_t sy = args.srcY + srcSlot->originY;
  bool ok = true;
  ForEachClip(*dstSlot, dx, dy, dx + static_cast<int32_t>(args.width), dy + static_cast<int32_t>(args.height),
              [&](int32_t l, int32_t t, int32_t r, int32_t b) {
                const int32_t csx = sx + (l - dx);
                const int32_t csy = sy + (t - dy);
                const auto w = static_cast<uint32_t>(r - l);
                const auto h = static_cast<uint32_t>(b - t);
                bool drawn = adapter->BitBlt && adapter->BitBlt(dstSlot->target, l, t, srcSlot->target, csx, csy, w, h, rop);
                if (!drawn) drawn = KE_GFX_SoftwareBitBlt(dstSlot->target, l, t, srcSlot->target, csx, csy, w, h, rop);
                ok = ok && drawn;
              });

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
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_GetBitmapBits_V1(__KRN_GFX_GETBITMAPBITS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto bitmap = ResolveSurface(*proc, args.bitmap);
  if (!bitmap || !bitmap->backing || !bitmap->backing->directAddress) return SyscallReturnCode_INVALID_ARGUMENT;
  if (!args.buffer) return SyscallReturnCode_INVALID_ARGUMENT;

  const size_t surfaceSize = bitmap->pitch * bitmap->height;
  const size_t copySize = args.buffer.size() < surfaceSize ? args.buffer.size() : surfaceSize;

  kstd::memcpy(args.buffer.get(), bitmap->backing->directAddress.as_ptr<uint8_t>(), copySize);
  args.buffer_len = copySize;

  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_SetBitmapBits_V1(__KRN_GFX_SETBITMAPBITS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto bitmap = ResolveSurface(*proc, args.bitmap);
  if (!bitmap || !bitmap->backing || !bitmap->backing->directAddress) return SyscallReturnCode_INVALID_ARGUMENT;
  if (!args.buffer) return SyscallReturnCode_INVALID_ARGUMENT;

  const size_t surfaceSize = bitmap->pitch * bitmap->height;
  const size_t copySize = args.buffer.size() < surfaceSize ? args.buffer.size() : surfaceSize;

  kstd::memcpy(bitmap->backing->directAddress.as_ptr<uint8_t>(), args.buffer.get(), copySize);

  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_GetDeviceCaps_V1(__KRN_GFX_GETDEVICECAPS_V1 &&args) {
  const auto proc = CurrentProcess();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto slot = proc->dcTable.Resolve(args.dc);
  if (!slot) return SyscallReturnCode_INVALID_ARGUMENT;

  slot->lock.lock();

  if (!slot->target) {
    slot->lock.unlock();
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  args.caps.width = slot->target->width;
  args.caps.height = slot->target->height;
  args.caps.colorDepth = BytesPerPixel(slot->target->pixelFormat) * 8;
  args.caps.accelFlags = 0;

  slot->lock.unlock();

  return SyscallReturnCode_OKAY;
}
