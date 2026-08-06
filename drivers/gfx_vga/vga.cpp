
#include <Kernel.hpp>

#include "vga_registers.hpp"

namespace {

constexpr size_t kVgaFramebufferSize = 0x10000;// 64 KiB (4 planes x 16 KiB window)
constexpr size_t kPageSize = 4096;

constexpr uint32_t kModeWidth = 640;
constexpr uint32_t kModeHeight = 480;
constexpr uint32_t kModePitch = kModeWidth / 8;// bytes per scanline, per plane

// Module-level state: this driver only ever handles a single, fixed VGA
// adapter/display, so there is no per-instance device context to allocate.
struct VgaContext {
  kernel::KObjectPtr<kernel::KGraphicsAdapter> adapter;
  kernel::KObjectPtr<kernel::KDisplay> display;
  kernel::VirtualAddress framebuffer;
};

VgaContext g_vga;

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
bool Vga_Probe() {
  // Legacy VGA hardware is always present once the driver is loaded (no
  // PnP participation, see UNDOS_DRIVER_ENTRY below).
  return true;
}

bool Vga_EnumerateDisplays() {
  return static_cast<bool>(g_vga.display);
}

size_t Vga_GetModeCount() {
  return 1;
}

bool Vga_GetModeInfo(size_t modeIndex, kernel::KDisplayModeInfo &outInfo) {
  if (modeIndex != 0) {
    return false;
  }

  outInfo.width = kModeWidth;
  outInfo.height = kModeHeight;
  outInfo.pitch = kModePitch;
  outInfo.bpp = 4;
  outInfo.refreshRate = 60;
  outInfo.pixelFormat = kernel::PixelFormat::INDEX8;
  outInfo.interlaced = false;
  outInfo.hwAccelerated = false;
  outInfo.textMode = false;
  return true;
}

bool Vga_SetMode(const kernel::KObjectPtr<kernel::KDisplay> &display, const kernel::KObjectPtr<kernel::KSurface>& outSurface, size_t modeIndex) {
  (void) display;
  if (modeIndex != 0 || !outSurface || !g_vga.framebuffer) {
    return false;
  }

  Vga_ProgramMode12h();

  outSurface->backing = KE_VMM_CreateDirectBacking(g_vga.framebuffer, kVgaFramebufferSize);
  if (!outSurface->backing) {
    return false;
  }

  outSurface->width = kModeWidth;
  outSurface->height = kModeHeight;
  outSurface->pitch = kModePitch;
  outSurface->pixelFormat = kernel::PixelFormat::INDEX8;
  return true;
}

void Vga_UnsetMode(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  (void) display;
  // Nothing to tear down: the framebuffer window stays mapped for the
  // lifetime of the driver.
}

bool Vga_FillRect(const kernel::KObjectPtr<kernel::KSurface> &dst, int x, int y, int w, int h, uint32_t colorIndex) {
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

bool Vga_DrawLine(const kernel::KObjectPtr<kernel::KSurface> &dst, int x0, int y0, int x1, int y1, uint32_t colorIndex) {
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

bool Vga_BitBlt(const kernel::KObjectPtr<kernel::KSurface> &dst,
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

void Vga_Present(const kernel::KObjectPtr<kernel::KSurface> &surface) {
  (void) surface;
  // The mapped window is already the visible framebuffer in mode 0x12:
  // there's no page-flipping to perform here.
}
// endregion

bool Vga_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = KE_IO_CreateDevice(driver, 0, "VGA16", kernel::device_type::Video);
  if (!fdo) {
    early_print("gfx_vga: Failed to create VGA16 device\r\n");
    return false;
  }

  KE_IO_AttachFilterDevice(fdo, pdo);
  return true;
}

void Vga_StartDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto driver = pdo->driverObject;

  // The framebuffer's physical address/length come from whatever the PnP
  // manager actually granted (via the Legacy VGA Enumerator's resource
  // requirements list), not from a hardcoded constant, mirroring
  // Ata_StartDevice's KE_RES_FindAssigned pattern.
  const auto *fbRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Memory, 0);
  if (!fbRes) {
    early_print("gfx_vga: no framebuffer memory resource assigned\r\n");
    return;
  }

  auto &as = KE_VMM_GetKernelAddressSpace();
  void *region = KE_VMM_AllocateRegion(as, fbRes->length, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!region) {
    early_print("gfx_vga: Failed to reserve virtual address space for the framebuffer\r\n");
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
  g_vga.framebuffer = virt;

  g_vga.adapter = KE_GFX_RegisterGraphicsAdapter(driver, pdo->attachedDevice, "VGA16");
  if (!g_vga.adapter) {
    early_print("gfx_vga: Failed to register graphics adapter\r\n");
    return;
  }

  g_vga.adapter->Probe = Vga_Probe;
  g_vga.adapter->EnumerateDisplays = Vga_EnumerateDisplays;
  g_vga.adapter->SetMode = Vga_SetMode;
  g_vga.adapter->UnsetMode = Vga_UnsetMode;
  g_vga.adapter->FillRect = Vga_FillRect;
  g_vga.adapter->DrawLine = Vga_DrawLine;
  g_vga.adapter->BitBlt = Vga_BitBlt;
  g_vga.adapter->Present = Vga_Present;

  g_vga.display = KE_GFX_RegisterDisplay(g_vga.adapter, "Display");
  if (!g_vga.display) {
    early_print("gfx_vga: Failed to register display\r\n");
    return;
  }

  g_vga.display->GetModeCount = Vga_GetModeCount;
  g_vga.display->GetModeInfo = Vga_GetModeInfo;

  early_print("gfx_vga: VGA16 (640x480x16) adapter registered\r\n");
}

}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = Vga_AddDevice;
  driver->StartDevice = Vga_StartDevice;
}
