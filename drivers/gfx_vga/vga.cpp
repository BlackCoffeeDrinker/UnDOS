
#include <Kernel.hpp>

#include "vga_registers.hpp"
#include "vga_palette.hpp"

#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__struct_IoctlDisplayMode.h>

namespace {

constexpr size_t kVgaFramebufferSize = 0x10000;// 64 KiB (4 planes x 16 KiB window)
constexpr size_t kPageSize = 4096;

constexpr uint32_t kModeWidth = 640;
constexpr uint32_t kModeHeight = 480;
constexpr uint32_t kModePitch = kModeWidth / 8;// bytes per scanline, per plane

// Per-instance state, held in the function device object's extension (see
// Vga_AddDevice); the driver keeps no mutable globals.
struct VgaContext {
  kernel::KObjectPtr<kernel::KGraphicsAdapter> adapter;
  kernel::VirtualAddress framebuffer;
};

// region Low-level register programming
void Vga_ProgramMode12h() {
  HAL_IO_Out8(vga_regs::kMiscOutputWrite, vga_regs::kMiscOutput);

  for (uint8_t i = 0; i < vga_regs::kSequencer.size(); ++i) {
    HAL_IO_Out8(vga_regs::kSequencerIndex, i);
    HAL_IO_Out8(vga_regs::kSequencerData, vga_regs::kSequencer[i]);
  }

  // Clear the CRTC write-protect bit (bit 7 of index 0x11) so indices 0-7
  // can actually be programmed below.
  HAL_IO_Out8(vga_regs::kCrtcIndex, 0x11);
  const uint8_t unlocked = HAL_IO_In8(vga_regs::kCrtcData) & 0x7F;
  HAL_IO_Out8(vga_regs::kCrtcData, unlocked);

  for (uint8_t i = 0; i < vga_regs::kCrtc.size(); ++i) {
    HAL_IO_Out8(vga_regs::kCrtcIndex, i);
    HAL_IO_Out8(vga_regs::kCrtcData, vga_regs::kCrtc[i]);
  }

  for (uint8_t i = 0; i < vga_regs::kGraphicsController.size(); ++i) {
    HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, i);
    HAL_IO_Out8(vga_regs::kGraphicsControllerData, vga_regs::kGraphicsController[i]);
  }

  // Reset the attribute controller's index/data flip-flop before writing.
  HAL_IO_In8(vga_regs::kInputStatus1);
  for (uint8_t i = 0; i < vga_regs::kAttributeController.size(); ++i) {
    HAL_IO_Out8(vga_regs::kAttributeControllerIndexData, i);
    HAL_IO_Out8(vga_regs::kAttributeControllerIndexData, vga_regs::kAttributeController[i]);
  }

  vga_regs::ProgramPalette16([](uint16_t port, uint8_t value) { HAL_IO_Out8(port, value); });

  // Re-enable video output (set the Palette Address Source bit).
  HAL_IO_In8(vga_regs::kInputStatus1);
  HAL_IO_Out8(vga_regs::kAttributeControllerIndexData, 0x20);
}

// Sets a single pixel through the classic "Set/Reset + Bit Mask" planar
// write technique: a read-modify-write of one byte in the mapped window
// touches the same bit across all 4 planes at once.
void Vga_PlotPixel(uint8_t *base, int x, int y, uint32_t colorIndex) {
  if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= kModeWidth || static_cast<uint32_t>(y) >= kModeHeight) {
    return;
  }

  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcSetReset);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, static_cast<uint8_t>(colorIndex & 0x0F));
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcEnableSetReset);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, 0x0F);
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcBitMask);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, static_cast<uint8_t>(0x80 >> (x % 8)));

  uint8_t *addr = base + static_cast<size_t>(y) * kModePitch + static_cast<size_t>(x) / 8;
  // A read loads the VGA's internal latches; the following write then
  // applies the Set/Reset color across all planes, masked by the bit mask.
  volatile uint8_t latch = *addr;
  (void) latch;
  *addr = 0xFF;
}

// Reads a single pixel's color index back by cycling the Read Map Select
// register across the 4 planes.
uint32_t Vga_ReadPixel(uint8_t *base, int x, int y) {
  const auto bit = static_cast<uint8_t>(0x80 >> (x % 8));
  const uint8_t *addr = base + static_cast<size_t>(y) * kModePitch + static_cast<size_t>(x) / 8;

  uint32_t colorIndex = 0;
  for (uint8_t plane = 0; plane < 4; ++plane) {
    HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcReadMapSelect);
    HAL_IO_Out8(vga_regs::kGraphicsControllerData, plane);
    if (*addr & bit) {
      colorIndex |= (1u << plane);
    }
  }
  return colorIndex;
}
// endregion

// region KGraphicsAdapter / KDisplay hooks
bool Vga_Probe(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/) {
  // Legacy VGA hardware is always present once the driver is loaded (no
  // PnP participation, see UNDOS_DRIVER_ENTRY below).
  return true;
}

// The single, fixed head this adapter drives. Identity only: the entry
// points are bound later, from Vga_ConfigureDisplay.
bool Vga_ProbeDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                      const kernel::KGraphicsAdapterDisplayEnumInfo &enumInfo,
                      kernel::KGraphicsAdapterDisplayInfo &outInfo) {
  if (enumInfo.displayIndex != 0) {
    return false;
  }

  outInfo.isPresent = true;
  outInfo.isMonitor = true;
  outInfo.isPnP = false;// No DDC/EDID on legacy VGA
  outInfo.edidSize = 0;
  outInfo.name = "VGA";
  return true;
}

bool Vga_GetModeInfo(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/, size_t modeIndex, kernel::KDisplayModeInfo &outInfo) {
  if (modeIndex != 0) {
    return false;
  }

  outInfo.modeIndex = 0;
  outInfo.width = kModeWidth;
  outInfo.height = kModeHeight;
  outInfo.pitch = kModePitch;
  outInfo.videoMemoryBitmapWidth = kModeWidth;
  outInfo.videoMemoryBitmapHeight = kModeHeight;
  outInfo.bpp = 4;
  outInfo.numberOfPlanes = 4;// Planar VGA: one bit of the index per plane
  outInfo.bitsPerPlane = 1;
  outInfo.refreshRate = 60;
  outInfo.pixelFormat = kernel::PixelFormat::INDEX8;
  outInfo.attributeFlags = kernel::DisplayModeFlags::GraphicsMode |
                           kernel::DisplayModeFlags::ColorMode |
                           kernel::DisplayModeFlags::PaletteDriven;
  return true;
}

bool Vga_SetMode(const kernel::KObjectPtr<kernel::KDisplay> &display, size_t modeIndex, kernel::KDisplaySurfaceBinding &outBinding) {
  if (modeIndex != 0 || !display || !display->adapter) {
    return false;
  }

  auto *ctx_vga = display->adapter->GetDeviceExtensionAs<VgaContext>();
  if (!ctx_vga || !ctx_vga->framebuffer) {
    return false;
  }

  auto backing = KE_VMM_CreateDirectBacking(ctx_vga->framebuffer, kVgaFramebufferSize);
  if (!backing) {
    KE_Print("gfx_vga: could not allocate mode backing, keeping the current mode");
    return false;
  }

  Vga_ProgramMode12h();

  // Clear all four planes: whatever text mode left in the window (font,
  // characters) otherwise shows up as garbage in mode 12h.
  HAL_IO_Out8(vga_regs::kSequencerIndex, 0x02);// Map Mask
  HAL_IO_Out8(vga_regs::kSequencerData, 0x0F);
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcEnableSetReset);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, 0x00);
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcBitMask);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, 0xFF);
  auto *fb = ctx_vga->framebuffer.as_ptr<volatile uint8_t>();
  for (size_t i = 0; i < static_cast<size_t>(kModePitch) * kModeHeight; ++i) {
    fb[i] = 0;
  }

  outBinding.backing = backing;

  outBinding.type = kernel::SurfaceType::Framebuffer;
  outBinding.flags = kernel::SurfaceFlags::TopDown;
  return true;
}

// Video output is gated by the Attribute Controller's Palette Address Source
// bit (0x20 written to the index port): set means "display", clear means
// "blank, registers accessible".
bool Vga_Enable(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/) {
  HAL_IO_In8(vga_regs::kInputStatus1);// Reset the index/data flip-flop
  HAL_IO_Out8(vga_regs::kAttributeControllerIndexData, 0x20);
  return true;
}

void Vga_Disable(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/) {
  HAL_IO_In8(vga_regs::kInputStatus1);
  HAL_IO_Out8(vga_regs::kAttributeControllerIndexData, 0x00);
}

// Binds this driver's per-head entry points on the display the graphics
// subsystem created for the head Vga_ProbeDisplay reported.
bool Vga_ConfigureDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/, const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display) {
    return false;
  }

  display->GetModeInfo = Vga_GetModeInfo;
  display->SetMode = Vga_SetMode;
  display->Enable = Vga_Enable;
  display->Disable = Vga_Disable;
  return true;
}

bool Vga_FillRect(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/, const kernel::KObjectPtr<kernel::KSurface> &dst, int x, int y, int w, int h, uint32_t colorIndex) {
  if (!dst || !dst->backing || !dst->backing->directAddress || w <= 0 || h <= 0) {
    return false;
  }

  auto *base = dst->backing->directAddress.as_ptr<uint8_t>();

  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcSetReset);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, static_cast<uint8_t>(colorIndex & 0x0F));
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcEnableSetReset);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, 0x0F);

  const int xEnd = x + w - 1;
  const int byteStart = x / 8;
  const int byteEnd = xEnd / 8;

  for (int row = y; row < y + h; ++row) {
    if (row < 0 || static_cast<uint32_t>(row) >= kModeHeight) continue;

    for (int b = byteStart; b <= byteEnd; ++b) {
      uint8_t mask = 0xFF;
      if (b == byteStart) mask &= static_cast<uint8_t>(0xFF >> (x % 8));
      if (b == byteEnd) mask &= static_cast<uint8_t>(0xFF << (7 - (xEnd % 8)));

      HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcBitMask);
      HAL_IO_Out8(vga_regs::kGraphicsControllerData, mask);

      uint8_t *addr = base + static_cast<size_t>(row) * kModePitch + static_cast<size_t>(b);
      volatile uint8_t latch = *addr;
      (void) latch;
      *addr = 0xFF;
    }
  }

  // Restore defaults so a stray direct memory access elsewhere doesn't
  // silently pick up a stale Set/Reset or Bit Mask state.
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcBitMask);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, 0xFF);
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcEnableSetReset);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, 0x00);
  return true;
}

bool Vga_DrawLine(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/, const kernel::KObjectPtr<kernel::KSurface> &dst, int x0, int y0, int x1, int y1, uint32_t colorIndex) {
  if (!dst || !dst->backing || !dst->backing->directAddress) {
    return false;
  }

  auto *base = dst->backing->directAddress.as_ptr<uint8_t>();

  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    Vga_PlotPixel(base, x0, y0, colorIndex);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
  return true;
}

bool Vga_ReadSurfacePixel(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &,
                          const kernel::KObjectPtr<kernel::KSurface> &src,
                          int x, int y, uint32_t &color) {
  if (!src || !src->backing || !src->backing->directAddress ||
      x < 0 || y < 0 || static_cast<uint32_t>(x) >= src->width ||
      static_cast<uint32_t>(y) >= src->height) return false;
  color = Vga_ReadPixel(src->backing->directAddress.as_ptr<uint8_t>(), x, y);
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcReadMapSelect);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, 0);
  return true;
}

bool Vga_BitBlt(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                const kernel::KObjectPtr<kernel::KSurface> &dst,
                int dstX, int dstY,
                const kernel::KObjectPtr<kernel::KSurface> &src,
                int srcX, int srcY,
                uint32_t width, uint32_t height,
                kernel::RasterOp rop) {
  if (rop != kernel::RasterOp::SRCCOPY) {
    // Anything other than a straight copy is left to the kernel's software
    // fallback (KE_GFX_Software*), which doesn't understand planar layouts.
    return false;
  }

  if (!dst || !dst->backing || !dst->backing->directAddress ||
      !src || !src->backing || !src->backing->directAddress) {
    return false;
  }

  auto *dstBase = dst->backing->directAddress.as_ptr<uint8_t>();
  auto *srcBase = src->backing->directAddress.as_ptr<uint8_t>();

  for (uint32_t row = 0; row < height; ++row) {
    for (uint32_t col = 0; col < width; ++col) {
      const uint32_t colorIndex = Vga_ReadPixel(srcBase, srcX + static_cast<int>(col), srcY + static_cast<int>(row));
      Vga_PlotPixel(dstBase, dstX + static_cast<int>(col), dstY + static_cast<int>(row), colorIndex);
    }
  }
  return true;
}

void Vga_Present(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/, const kernel::KObjectPtr<kernel::KSurface> &surface, const kernel::KClipRect *rects, size_t count) {
  (void) surface;
  (void) rects;
  (void) count;
  // The mapped window is already the visible framebuffer in mode 0x12:
  // there's no page-flipping to perform here.
}
// endregion

bool Vga_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = KE_IO_CreateDeviceWithContext<VgaContext>(driver, "VGA16", kernel::device_type::Video);
  if (!fdo) {
    KE_Print("gfx_vga: Failed to create VGA16 device");
    return false;
  }

  KE_IO_AttachFilterDevice(fdo, pdo);
  return true;
}

kernel::IoStatus Vga_DeviceControl(const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t code,
                                   kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred) {
  (void) device;
  (void) input;
  transferred = 0;
  if (code != IoControlCode_DisplayQueryMode) return kernel::IoStatus::Unsupported;
  if (output.size() < sizeof(IoctlDisplayMode)) return kernel::IoStatus::InvalidSize;

  const IoctlDisplayMode mode{kModeWidth, kModeHeight, 4, kModePitch};
  __builtin_memcpy(output.data(), &mode, sizeof(mode));
  transferred = sizeof(mode);
  KE_Print("gfx_vga: DeviceControl QueryMode -> {}x{}x{}", mode.width, mode.height, mode.bpp);
  return kernel::IoStatus::Success;
}

void Vga_StartDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto driver = pdo->driverObject;

  // The context lives in the function device object Vga_AddDevice stacked on
  // top of the PDO; that same device is the one the adapter points at, so the
  // hooks resolve the very same state through GetDeviceExtensionAs.
  const auto fdo = pdo->attachedDevice ? pdo->attachedDevice : pdo;
  auto *ctx_vga = fdo->deviceExtension.as<VgaContext>();
  if (!ctx_vga) {
    KE_Print("gfx_vga: no device context");
    return;
  }

  // The framebuffer's physical address/length come from whatever the PnP
  // manager actually granted (via the Legacy VGA Enumerator's resource
  // requirements list), not from a hardcoded constant, mirroring
  // Ata_StartDevice's KE_RES_FindAssigned pattern.
  const auto *fbRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Memory, 0);
  if (!fbRes) {
    KE_Print("gfx_vga: no framebuffer memory resource assigned");
    return;
  }

  auto &as = KE_VMM_GetKernelAddressSpace();
  void *region = KE_VMM_AllocateRegion(as, fbRes->length, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!region) {
    KE_Print("gfx_vga: Failed to reserve virtual address space for the framebuffer");
    return;
  }

  const auto virt = kernel::VirtualAddress::from_ptr(region);
  const size_t pageCount = fbRes->length / kPageSize;
  for (size_t i = 0; i < pageCount; ++i) {
    KE_VMM_MapPhysical(
        as,
        virt + (i * kPageSize),
        kernel::PhysicalAddress(fbRes->start) + (i * kPageSize),
        kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  }
  ctx_vga->framebuffer = virt;

  ctx_vga->adapter = KE_GFX_RegisterGraphicsAdapter(driver, fdo, "VGA16");
  if (!ctx_vga->adapter) {
    KE_Print("gfx_vga: Failed to register graphics adapter");
    return;
  }

  ctx_vga->adapter->Probe = Vga_Probe;
  ctx_vga->adapter->ProbeDisplay = Vga_ProbeDisplay;
  ctx_vga->adapter->ConfigureDisplay = Vga_ConfigureDisplay;
  ctx_vga->adapter->FillRect = Vga_FillRect;
  ctx_vga->adapter->ReadPixel = Vga_ReadSurfacePixel;
  ctx_vga->adapter->DrawLine = Vga_DrawLine;
  ctx_vga->adapter->BitBlt = Vga_BitBlt;
  ctx_vga->adapter->Present = Vga_Present;

  KE_Print("gfx_vga: VGA16 (640x480x16) adapter registered");
}

}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = Vga_AddDevice;
  driver->StartDevice = Vga_StartDevice;
  driver->DeviceControl = Vga_DeviceControl;
}
