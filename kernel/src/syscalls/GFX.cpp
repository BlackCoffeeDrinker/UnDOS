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
    display->surface = KE_GFX_CreateSurface(display, 0);
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
  const size_t pitch = static_cast<size_t>(args.width) * bpp;
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

  bool ok = adapter->DrawLine(slot->target, slot->currentX, slot->currentY, args.x, args.y, slot->penColor);
  if (!ok) {
    ok = KE_GFX_SoftwareDrawLine(slot->target, slot->currentX, slot->currentY, args.x, args.y, slot->penColor);
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

  const int w = args.right - args.left;
  const int h = args.bottom - args.top;

  bool ok = adapter->FillRect && adapter->FillRect(slot->target, args.left, args.top, w, h, slot->penColor);
  if (!ok) {
    ok = KE_GFX_SoftwareFillRect(slot->target, args.left, args.top, w, h, slot->penColor);
  }

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

  bool ok = adapter->BitBlt && adapter->BitBlt(dstSlot->target, args.x, args.y, srcSlot->target, args.srcX, args.srcY, args.width, args.height, rop);
  if (!ok) {
    ok = KE_GFX_SoftwareBitBlt(dstSlot->target, args.x, args.y, srcSlot->target, args.srcX, args.srcY, args.width, args.height, rop);
  }

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

  const size_t surfaceSize = static_cast<size_t>(bitmap->pitch) * bitmap->height;
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

  const size_t surfaceSize = static_cast<size_t>(bitmap->pitch) * bitmap->height;
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
