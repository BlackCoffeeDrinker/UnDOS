
#include <Kernel.hpp>

#include "vga_registers.hpp"
#include "vga_palette.hpp"
#include "vga_mode.hpp"

#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__struct_IoctlDisplayMode.h>

namespace {

constexpr size_t kVgaFramebufferSize = 0x10000;// 64 KiB (4 planes x 16 KiB window)
constexpr size_t kPageSize = 4096;

constexpr uint32_t kModeWidth = kernel::gfx::vga::ModeWidth;
constexpr uint32_t kModeHeight = kernel::gfx::vga::ModeHeight;
constexpr uint32_t kModePitch = kernel::gfx::vga::ModePitch;

// Per-instance state, held in the function device object's extension (see
// Vga_AddDevice); the driver keeps no mutable globals.
struct VgaContext {
  kernel::KObjectPtr<kernel::KGraphicsAdapter> adapter;
  kernel::VirtualAddress framebuffer;
  bool modeKnown{false};
  bool enabled{false};
  size_t modeIndex{};
};

// region Low-level register programming
void Vga_ProgramMode12h() {
  HAL_IO_Out8(vga_regs::kMiscOutputWrite, vga_regs::kMiscOutput);

  for (uint8_t i = 0; i < vga_regs::kSequencer.size(); ++i) {
    HAL_IO_Out8(vga_regs::kSequencerIndex, i);
    HAL_IO_Out8(vga_regs::kSequencerData, static_cast<uint8_t>(
        vga_regs::kSequencer[i] | (i == 1 ? 0x20 : 0)));
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
  const volatile uint8_t latch = *addr;
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
kstd::error_code Vga_Probe(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/) {
  // Legacy VGA hardware is always present once the driver is loaded (no
  // PnP participation, see UNDOS_DRIVER_ENTRY below).
  return kernel::KernelError::Success;
}

// The single, fixed head this adapter drives. Identity only: the entry
// points are bound later, from Vga_ConfigureDisplay.
kstd::error_code Vga_ProbeDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                             const kernel::KGraphicsAdapterDisplayEnumInfo &enumInfo,
                             kernel::KGraphicsAdapterDisplayInfo &outInfo) {
  if (enumInfo.displayIndex != 0) {
    return kernel::KernelError::OutOfRange;
  }

  outInfo.isPresent = true;
  outInfo.isMonitor = true;
  outInfo.isPnP = false;// No DDC/EDID on legacy VGA
  outInfo.edidSize = 0;
  outInfo.name = "VGA";
  return kernel::KernelError::Success;
}

kstd::expected<kernel::KDisplayModeInfo, kstd::error_code> Vga_GetModeInfo(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/,
                                                                       size_t modeIndex) {
  kernel::KDisplayModeInfo outInfo{};
  if (!kernel::gfx::vga::GetModeInfo(modeIndex, outInfo)) {
    return kstd::unexpected(kernel::KernelError::OutOfRange);
  }
  return outInfo;
}

kstd::error_code Vga_PrepareMode(const kernel::KObjectPtr<kernel::KDisplay> &display,
                                 size_t modeIndex,
                                 kernel::KDisplayModeReservation &reservation) {
  if (modeIndex != 0) {
    return kernel::KernelError::OutOfRange;
  }
  if (!display || !display->adapter) {
    return kernel::KernelError::InvalidArgument;
  }

  const auto *ctx_vga = display->adapter->GetDeviceExtensionAs<VgaContext>();
  if (!ctx_vga || !ctx_vga->framebuffer) {
    return kernel::KernelError::DeviceNotReady;
  }

  auto backing = KE_VMM_CreateDirectBacking(ctx_vga->framebuffer, kVgaFramebufferSize);
  if (!backing) {
    KE_Print("gfx_vga: could not allocate mode backing, keeping the current mode: {}", backing.error().message());
    return backing.error();
  }

  auto mode = Vga_GetModeInfo(display, modeIndex);
  if (!mode) return mode.error();
  reservation = {};
  reservation.modeIndex = modeIndex;
  reservation.mode = *mode;
  reservation.binding.backing = kstd::move(*backing);
  reservation.binding.type = kernel::SurfaceType::Framebuffer;
  reservation.binding.flags = kernel::SurfaceFlags::TopDown;
  return kernel::KernelError::Success;
}

kstd::error_code Vga_ProgramMode(const kernel::KObjectPtr<kernel::KDisplay> &display,
                                 kernel::KDisplayModeReservation &reservation) {
  if (!display || !display->adapter || reservation.modeIndex != 0 ||
      reservation.mode.width != kModeWidth || reservation.mode.height != kModeHeight ||
      !reservation.binding.backing)
    return kernel::KernelError::InvalidArgument;
  const auto *ctx_vga = display->adapter->GetDeviceExtensionAs<VgaContext>();
  if (!ctx_vga || !ctx_vga->framebuffer) return kernel::KernelError::DeviceNotReady;

  Vga_ProgramMode12h();

  HAL_IO_Out8(vga_regs::kSequencerIndex, 0x02);// Map Mask
  HAL_IO_Out8(vga_regs::kSequencerData, 0x0F);
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcEnableSetReset);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, 0x00);
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcBitMask);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, 0xFF);
  auto *fb = reservation.binding.backing->directAddress.as_ptr<volatile uint8_t>();
  for (size_t i = 0; i < static_cast<size_t>(kModePitch) * kModeHeight; ++i)
    fb[i] = 0;

  auto *mutableContext = display->adapter->GetDeviceExtensionAs<VgaContext>();
  mutableContext->modeKnown = true;
  mutableContext->modeIndex = reservation.modeIndex;
  return kernel::KernelError::Success;
}

void Vga_FinishMode(const kernel::KObjectPtr<kernel::KDisplay> & /*display*/,
                    kernel::KDisplayModeReservation & /*reservation*/,
                    bool /*committed*/) {}

kstd::error_code Vga_RestoreMode(const kernel::KObjectPtr<kernel::KDisplay> &display,
                                 size_t modeIndex,
                                 kernel::KDisplayModeReservation & /*reservation*/) {
  if (!display || !display->adapter || modeIndex != 0)
    return kernel::KernelError::Unsupported;
  const auto *ctx_vga = display->adapter->GetDeviceExtensionAs<VgaContext>();
  if (!ctx_vga || !ctx_vga->framebuffer) return kernel::KernelError::DeviceNotReady;
  Vga_ProgramMode12h();
  auto *mutableContext = display->adapter->GetDeviceExtensionAs<VgaContext>();
  mutableContext->modeKnown = true;
  mutableContext->modeIndex = modeIndex;
  return kernel::KernelError::Success;
}

kstd::expected<kernel::KDisplayActualState, kstd::error_code>
Vga_QueryActualState(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display || !display->adapter)
    return kstd::unexpected(kernel::KernelError::InvalidArgument);
  const auto *ctx_vga = display->adapter->GetDeviceExtensionAs<VgaContext>();
  if (!ctx_vga) return kstd::unexpected(kernel::KernelError::DeviceNotReady);
  return kernel::KDisplayActualState{
      .known = ctx_vga->modeKnown,
      .enabled = ctx_vga->enabled,
      .modeIndex = ctx_vga->modeIndex};
}

// Video output is gated by the Attribute Controller's Palette Address Source
// bit (0x20 written to the index port): set means "display", clear means
// "blank, registers accessible".
kstd::error_code Vga_Enable(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display || !display->adapter) return kernel::KernelError::InvalidArgument;
  HAL_IO_In8(vga_regs::kInputStatus1);// Reset the index/data flip-flop
  HAL_IO_Out8(vga_regs::kAttributeControllerIndexData, 0x20);
  auto *context = display->adapter->GetDeviceExtensionAs<VgaContext>();
  if (!context) return kernel::KernelError::DeviceNotReady;
  context->enabled = true;
  return kernel::KernelError::Success;
}

kstd::error_code Vga_Disable(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  HAL_IO_In8(vga_regs::kInputStatus1);
  HAL_IO_Out8(vga_regs::kAttributeControllerIndexData, 0x00);
  if (display && display->adapter) {
    if (auto *context = display->adapter->GetDeviceExtensionAs<VgaContext>())
      context->enabled = false;
  }
  return kernel::KernelError::Success;
}

kstd::error_code Vga_Validate(const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display || !display->adapter || !display->isPresent)
    return kernel::KernelError::NotFound;
  const auto *context = display->adapter->GetDeviceExtensionAs<VgaContext>();
  return context && context->framebuffer
             ? kernel::KernelError::Success
             : kernel::KernelError::DeviceNotReady;
}

// Binds this driver's per-head entry points on the display the graphics
// subsystem created for the head Vga_ProbeDisplay reported.
kstd::error_code Vga_ConfigureDisplay(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/, const kernel::KObjectPtr<kernel::KDisplay> &display) {
  if (!display) {
    return kernel::KernelError::InvalidArgument;
  }

  display->GetModeInfo = Vga_GetModeInfo;
  display->PrepareMode = Vga_PrepareMode;
  display->ProgramMode = Vga_ProgramMode;
  display->FinishMode = Vga_FinishMode;
  display->RestoreMode = Vga_RestoreMode;
  display->QueryActualState = Vga_QueryActualState;
  display->Enable = Vga_Enable;
  display->Disable = Vga_Disable;
  display->Validate = Vga_Validate;
  return kernel::KernelError::Success;
}

kstd::error_code Vga_FillRect(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/, const kernel::KObjectPtr<kernel::KSurface> &dst, int x, int y, int w, int h, uint32_t colorIndex) {
  if (!dst || !dst->backing || !dst->backing->directAddress) {
    return kernel::KernelError::Unsupported;
  }
  if (w <= 0 || h <= 0) {
    return kernel::KernelError::InvalidSize;
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
      const volatile uint8_t latch = *addr;
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
  return kernel::KernelError::Success;
}

kstd::error_code Vga_DrawLine(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/, const kernel::KObjectPtr<kernel::KSurface> &dst, int x0, int y0, int x1, int y1, uint32_t colorIndex) {
  if (!dst || !dst->backing || !dst->backing->directAddress) {
    return kernel::KernelError::Unsupported;
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
  return kernel::KernelError::Success;
}

kstd::expected<uint32_t, kstd::error_code> Vga_ReadSurfacePixel(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &,
                                                            const kernel::KObjectPtr<kernel::KSurface> &src,
                                                            int x, int y) {
  if (!src || !src->backing || !src->backing->directAddress) return kstd::unexpected(kernel::KernelError::Unsupported);
  if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= src->width ||
      static_cast<uint32_t>(y) >= src->height) return kstd::unexpected(kernel::KernelError::OutOfRange);
  const uint32_t color = Vga_ReadPixel(src->backing->directAddress.as_ptr<uint8_t>(), x, y);
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, vga_regs::kGcReadMapSelect);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, 0);
  return color;
}

kstd::error_code Vga_BitBlt(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/,
                const kernel::KObjectPtr<kernel::KSurface> &dst,
                int dstX, int dstY,
                const kernel::KObjectPtr<kernel::KSurface> &src,
                int srcX, int srcY,
                uint32_t width, uint32_t height,
                kernel::RasterOp rop) {
  if (rop != kernel::RasterOp::SRCCOPY) {
    // Anything other than a straight copy is left to the kernel's software
    // fallback (KE_GFX_Software*), which doesn't understand planar layouts.
    return kernel::KernelError::Unsupported;
  }

  if (!dst || !dst->backing || !dst->backing->directAddress ||
      !src || !src->backing || !src->backing->directAddress) {
    return kernel::KernelError::Unsupported;
  }

  auto *dstBase = dst->backing->directAddress.as_ptr<uint8_t>();
  auto *srcBase = src->backing->directAddress.as_ptr<uint8_t>();

  for (uint32_t row = 0; row < height; ++row) {
    for (uint32_t col = 0; col < width; ++col) {
      const uint32_t colorIndex = Vga_ReadPixel(srcBase, srcX + static_cast<int>(col), srcY + static_cast<int>(row));
      Vga_PlotPixel(dstBase, dstX + static_cast<int>(col), dstY + static_cast<int>(row), colorIndex);
    }
  }
  return kernel::KernelError::Success;
}

void Vga_Present(const kernel::KObjectPtr<kernel::KGraphicsAdapter> & /*adapter*/, const kernel::KObjectPtr<kernel::KSurface> &surface, const kernel::KClipRect *rects, size_t count) {
  (void) surface;
  (void) rects;
  (void) count;
  // The mapped window is already the visible framebuffer in mode 0x12:
  // there's no page-flipping to perform here.
}
// endregion

kstd::error_code Vga_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = KE_IO_CreateDeviceWithContext<VgaContext>(driver, "VGA16", kernel::device_type::Video);
  if (!fdo) {
    KE_Print("gfx_vga: Failed to create VGA16 device: {}", fdo.error().message());
    return fdo.error();
  }

  if (const auto attached = KE_IO_AttachFilterDevice(*fdo, pdo); !attached) {
    KE_Print("gfx_vga: Failed to attach VGA16 device: {}", attached.error().message());
    return attached.error();
  }
  return kernel::KernelError::Success;
}

kstd::error_code Vga_DeviceControl(const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t code,
                                   kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred) {
  (void) device;
  (void) input;
  transferred = 0;
  if (code != IoControlCode_DisplayQueryMode) return kernel::KernelError::Unsupported;
  if (output.size() < sizeof(IoctlDisplayMode)) return kernel::KernelError::BufferTooSmall;

  const IoctlDisplayMode mode{.width = kModeWidth, .height = kModeHeight, .bpp = 4, .pitch = kModePitch};
  __builtin_memcpy(output.data(), &mode, sizeof(mode));
  transferred = sizeof(mode);
  KE_Print("gfx_vga: DeviceControl QueryMode -> {}x{}x{}", mode.width, mode.height, mode.bpp);
  return kernel::KernelError::Success;
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
  const auto region = KE_VMM_AllocateRegion(as, fbRes->length, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!region) {
    KE_Print("gfx_vga: Failed to reserve virtual address space for the framebuffer: {}", region.error().message());
    return;
  }

  const auto virt = kernel::VirtualAddress::from_ptr(*region);
  const size_t pageCount = fbRes->length / kPageSize;
  for (size_t i = 0; i < pageCount; ++i) {
    if (const auto mapped = KE_VMM_MapPhysical(
            as,
            virt + (i * kPageSize),
            kernel::PhysicalAddress(fbRes->start) + (i * kPageSize),
            kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
        mapped) {
      KE_Print("gfx_vga: Failed to map the framebuffer: {}", mapped.message());
      return;
    }
  }
  ctx_vga->framebuffer = virt;

  auto adapter = KE_GFX_RegisterGraphicsAdapter(driver, fdo, "VGA16");
  if (!adapter) {
    KE_Print("gfx_vga: Failed to register graphics adapter: {}", adapter.error().message());
    return;
  }
  ctx_vga->adapter = kstd::move(*adapter);

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
