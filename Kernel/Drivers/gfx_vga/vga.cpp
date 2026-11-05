
#include <Kernel.hpp>

#include "vga_mode.hpp"
#include "vga_palette.hpp"
#include "vga_registers.hpp"

#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__struct_IoctlDisplayMode.h>

namespace {

constexpr size_t kVgaFramebufferSize = 0x10000;// 64 KiB (4 planes x 16 KiB window)
constexpr size_t kPageSize = 4096;

constexpr uint32_t kModeWidth = kernel::gfx::vga::ModeWidth;
constexpr uint32_t kModeHeight = kernel::gfx::vga::ModeHeight;
constexpr uint32_t kModePitch = kernel::gfx::vga::ModePitch;
constexpr uint8_t kGcMode = 0x05;
constexpr uint32_t kShadowStride = kModeWidth;
constexpr size_t kShadowSize = static_cast<size_t>(kShadowStride) * kModeHeight;

// Per-instance state, held in the function device object's extension (see
// Vga_AddDevice); the driver keeps no mutable globals.
struct VgaContext {
  kernel::KObjectPtr<kernel::KGraphicsAdapter> adapter;
  kernel::VirtualAddress framebuffer;
  uint8_t *shadow{nullptr};
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

struct VgaModeData {
  uint8_t *candidate{nullptr};
  uint8_t *previous{nullptr};
  bool installed{false};
};

void WriteSequencer(uint8_t index, uint8_t value) {
  HAL_IO_Out8(vga_regs::kSequencerIndex, index);
  HAL_IO_Out8(vga_regs::kSequencerData, value);
}

uint8_t ReadSequencer(uint8_t index) {
  HAL_IO_Out8(vga_regs::kSequencerIndex, index);
  return HAL_IO_In8(vga_regs::kSequencerData);
}

void WriteGraphics(uint8_t index, uint8_t value) {
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, index);
  HAL_IO_Out8(vga_regs::kGraphicsControllerData, value);
}

uint8_t ReadGraphics(uint8_t index) {
  HAL_IO_Out8(vga_regs::kGraphicsControllerIndex, index);
  return HAL_IO_In8(vga_regs::kGraphicsControllerData);
}

uint8_t Vga_EncodePlaneByte(const uint8_t *shadow, uint32_t y,
                            uint32_t byte, uint8_t plane) {
  uint8_t result = 0;
  for (uint32_t bit = 0; bit < 8; ++bit) {
    const uint32_t x = byte * 8 + bit;
    if (shadow[static_cast<size_t>(y) * kShadowStride + x] & (1u << plane))
      result |= static_cast<uint8_t>(0x80u >> bit);
  }
  return result;
}

void Vga_PresentShadow(uint8_t *shadow, volatile uint8_t *framebuffer,
                       const kernel::KClipRect *rects, size_t count) {
  if (!shadow || !framebuffer || (!rects && count)) return;
  const uint8_t oldMapMask = ReadSequencer(0x02);
  const uint8_t oldSetReset = ReadGraphics(vga_regs::kGcSetReset);
  const uint8_t oldEnableSetReset = ReadGraphics(vga_regs::kGcEnableSetReset);
  const uint8_t oldReadMap = ReadGraphics(vga_regs::kGcReadMapSelect);
  const uint8_t oldMode = ReadGraphics(kGcMode);
  const uint8_t oldBitMask = ReadGraphics(vga_regs::kGcBitMask);

  WriteGraphics(vga_regs::kGcEnableSetReset, 0x00);
  WriteGraphics(kGcMode, static_cast<uint8_t>(oldMode & ~0x03u));
  WriteGraphics(vga_regs::kGcBitMask, 0xFF);
  for (size_t i = 0; i < count; ++i) {
    const auto &requested = rects[i];
    const int32_t left = requested.left < 0 ? 0 : requested.left;
    const int32_t top = requested.top < 0 ? 0 : requested.top;
    const int32_t right = requested.right > static_cast<int32_t>(kModeWidth)
                              ? static_cast<int32_t>(kModeWidth)
                              : requested.right;
    const int32_t bottom = requested.bottom > static_cast<int32_t>(kModeHeight)
                               ? static_cast<int32_t>(kModeHeight)
                               : requested.bottom;
    if (right <= left || bottom <= top) continue;
    const int32_t firstByte = left / 8;
    const int32_t lastByte = (right - 1) / 8;
    for (int32_t y = top; y < bottom; ++y) {
      for (int32_t byte = firstByte; byte <= lastByte; ++byte) {
        const size_t offset = static_cast<size_t>(y) * kModePitch +
                              static_cast<size_t>(byte);
        for (uint8_t plane = 0; plane < 4; ++plane) {
          WriteSequencer(0x02, static_cast<uint8_t>(1u << plane));
          framebuffer[offset] = Vga_EncodePlaneByte(
              shadow, static_cast<uint32_t>(y), static_cast<uint32_t>(byte), plane);
        }
      }
    }
  }
  WriteSequencer(0x02, oldMapMask);
  WriteGraphics(vga_regs::kGcSetReset, oldSetReset);
  WriteGraphics(vga_regs::kGcEnableSetReset, oldEnableSetReset);
  WriteGraphics(vga_regs::kGcReadMapSelect, oldReadMap);
  WriteGraphics(kGcMode, oldMode);
  WriteGraphics(vga_regs::kGcBitMask, oldBitMask);
}

void Vga_PlotPixel(uint8_t *base, int x, int y, uint32_t colorIndex) {
  if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= kModeWidth ||
      static_cast<uint32_t>(y) >= kModeHeight) return;
  base[static_cast<size_t>(y) * kShadowStride + static_cast<size_t>(x)] =
      static_cast<uint8_t>(colorIndex & 0x0F);
}

uint32_t Vga_ReadPixel(const uint8_t *base, int x, int y) {
  return base[static_cast<size_t>(y) * kShadowStride + static_cast<size_t>(x)] & 0x0F;
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
  if (modeIndex != 0) return kernel::KernelError::OutOfRange;
  if (!display || !display->adapter) return kernel::KernelError::InvalidArgument;
  const auto *context = display->adapter->GetDeviceExtensionAs<VgaContext>();
  if (!context || !context->framebuffer)
    return kernel::KernelError::DeviceNotReady;
  auto mode = Vga_GetModeInfo(display, modeIndex);
  if (!mode) return mode.error();

  auto *transaction = static_cast<VgaModeData *>(KE_Malloc(sizeof(VgaModeData)));
  if (!transaction) return kernel::KernelError::OutOfMemory;
  *transaction = {};
  transaction->candidate = static_cast<uint8_t *>(KE_Malloc(kShadowSize));
  if (!transaction->candidate) {
    KE_Free(transaction);
    return kernel::KernelError::OutOfMemory;
  }
  __builtin_memset(transaction->candidate, 0, kShadowSize);
  auto backing = KE_VMM_CreateDirectBacking(
      kernel::VirtualAddress::from_ptr(transaction->candidate), kShadowSize);
  if (!backing) {
    KE_Free(transaction->candidate);
    KE_Free(transaction);
    return backing.error();
  }

  reservation = {};
  reservation.modeIndex = modeIndex;
  reservation.mode = *mode;
  reservation.driverData = transaction;
  reservation.binding.backing = kstd::move(*backing);
  reservation.binding.type = kernel::SurfaceType::Framebuffer;
  reservation.binding.flags = kernel::SurfaceFlags::Linear | kernel::SurfaceFlags::TopDown;
  reservation.binding.surfaceStride = kShadowStride;
  return kernel::KernelError::Success;
}

kstd::error_code Vga_ProgramMode(const kernel::KObjectPtr<kernel::KDisplay> &display,
                                 kernel::KDisplayModeReservation &reservation) {
  auto *context = display && display->adapter
                      ? display->adapter->GetDeviceExtensionAs<VgaContext>()
                      : nullptr;
  auto *transaction = static_cast<VgaModeData *>(reservation.driverData);
  if (!context || !context->framebuffer || !transaction ||
      !transaction->candidate || reservation.modeIndex != 0 ||
      reservation.mode.width != kModeWidth || reservation.mode.height != kModeHeight ||
      reservation.binding.surfaceStride != kShadowStride)
    return kernel::KernelError::InvalidArgument;

  Vga_ProgramMode12h();
  transaction->previous = context->shadow;
  context->shadow = transaction->candidate;
  transaction->installed = true;
  const kernel::KClipRect full{0, 0, static_cast<int32_t>(kModeWidth),
                               static_cast<int32_t>(kModeHeight)};
  Vga_PresentShadow(context->shadow,
                    context->framebuffer.as_ptr<volatile uint8_t>(), &full, 1);
  context->modeKnown = true;
  context->modeIndex = reservation.modeIndex;
  return kernel::KernelError::Success;
}

void Vga_FinishMode(const kernel::KObjectPtr<kernel::KDisplay> &display,
                    kernel::KDisplayModeReservation &reservation,
                    bool committed) {
  auto *context = display && display->adapter
                      ? display->adapter->GetDeviceExtensionAs<VgaContext>()
                      : nullptr;
  auto *transaction = static_cast<VgaModeData *>(reservation.driverData);
  if (!transaction) return;
  if (committed) {
    if (transaction->previous) KE_Free(transaction->previous);
  } else {
    if (transaction->installed && context && context->shadow == transaction->candidate) {
      context->shadow = transaction->previous;
      if (context->shadow) {
        Vga_ProgramMode12h();
        kernel::KClipRect full{0, 0, static_cast<int32_t>(kModeWidth),
                               static_cast<int32_t>(kModeHeight)};
        Vga_PresentShadow(context->shadow,
                          context->framebuffer.as_ptr<volatile uint8_t>(), &full, 1);
      }
    }
    KE_Free(transaction->candidate);
  }
  KE_Free(transaction);
}

kstd::error_code Vga_RestoreMode(const kernel::KObjectPtr<kernel::KDisplay> &display,
                                 size_t modeIndex,
                                 kernel::KDisplayModeReservation &reservation) {
  if (!display || !display->adapter || modeIndex != 0)
    return kernel::KernelError::Unsupported;
  auto *context = display->adapter->GetDeviceExtensionAs<VgaContext>();
  auto *transaction = static_cast<VgaModeData *>(reservation.driverData);
  if (!context || !context->framebuffer || !transaction || !transaction->previous)
    return kernel::KernelError::DeviceNotReady;
  Vga_ProgramMode12h();
  context->shadow = transaction->previous;
  const kernel::KClipRect full{0, 0, static_cast<int32_t>(kModeWidth),
                               static_cast<int32_t>(kModeHeight)};
  Vga_PresentShadow(context->shadow,
                    context->framebuffer.as_ptr<volatile uint8_t>(), &full, 1);
  context->modeKnown = true;
  context->modeIndex = modeIndex;
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

kstd::error_code Vga_FillRect(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &,
                              const kernel::KObjectPtr<kernel::KSurface> &dst,
                              int x, int y, int w, int h, uint32_t colorIndex) {
  if (!dst || !dst->backing || !dst->backing->directAddress)
    return kernel::KernelError::Unsupported;
  if (w <= 0 || h <= 0) return kernel::KernelError::InvalidSize;
  if (dst->pitch <= 0 || static_cast<uint32_t>(dst->pitch) < dst->width ||
      static_cast<uint64_t>(dst->pitch) * dst->height > dst->sizeInBytes)
    return kernel::KernelError::InvalidSize;
  const int64_t left = x < 0 ? 0 : x;
  const int64_t top = y < 0 ? 0 : y;
  const int64_t right = static_cast<int64_t>(x) + w > dst->width
                            ? dst->width
                            : static_cast<int64_t>(x) + w;
  const int64_t bottom = static_cast<int64_t>(y) + h > dst->height
                             ? dst->height
                             : static_cast<int64_t>(y) + h;
  auto *base = dst->backing->directAddress.as_ptr<uint8_t>();
  for (int64_t row = top; row < bottom; ++row)
    __builtin_memset(base + static_cast<size_t>(row) * static_cast<size_t>(dst->pitch) +
                         static_cast<size_t>(left),
                     static_cast<uint8_t>(colorIndex & 0x0F),
                     static_cast<size_t>(right - left));
  return kernel::KernelError::Success;
}

kstd::error_code Vga_DrawLine(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &,
                              const kernel::KObjectPtr<kernel::KSurface> &dst,
                              int x0, int y0, int x1, int y1, uint32_t colorIndex) {
  if (!dst || !dst->backing || !dst->backing->directAddress)
    return kernel::KernelError::Unsupported;
  auto *base = dst->backing->directAddress.as_ptr<uint8_t>();
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    Vga_PlotPixel(base, x0, y0, colorIndex);
    if (x0 == x1 && y0 == y1) break;
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

kstd::expected<uint32_t, kstd::error_code> Vga_ReadSurfacePixel(
    const kernel::KObjectPtr<kernel::KGraphicsAdapter> &,
    const kernel::KObjectPtr<kernel::KSurface> &src, int x, int y) {
  if (!src || !src->backing || !src->backing->directAddress)
    return kstd::unexpected(kernel::KernelError::Unsupported);
  if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= src->width ||
      static_cast<uint32_t>(y) >= src->height)
    return kstd::unexpected(kernel::KernelError::OutOfRange);
  return Vga_ReadPixel(src->backing->directAddress.as_ptr<uint8_t>(), x, y);
}

kstd::error_code Vga_BitBlt(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &,
                            const kernel::KObjectPtr<kernel::KSurface> &dst,
                            int dstX, int dstY,
                            const kernel::KObjectPtr<kernel::KSurface> &src,
                            int srcX, int srcY,
                            uint32_t width, uint32_t height,
                            kernel::RasterOp rop) {
  if (rop != kernel::RasterOp::SRCCOPY) return kernel::KernelError::Unsupported;
  if (!dst || !dst->backing || !dst->backing->directAddress ||
      !src || !src->backing || !src->backing->directAddress)
    return kernel::KernelError::Unsupported;
  auto *dstBase = dst->backing->directAddress.as_ptr<uint8_t>();
  const auto *srcBase = src->backing->directAddress.as_ptr<uint8_t>();
  for (uint32_t row = 0; row < height; ++row) {
    for (uint32_t col = 0; col < width; ++col) {
      const int x = dstX + static_cast<int>(col);
      const int y = dstY + static_cast<int>(row);
      const int sourceX = srcX + static_cast<int>(col);
      const int sourceY = srcY + static_cast<int>(row);
      if (x < 0 || y < 0 || sourceX < 0 || sourceY < 0 ||
          static_cast<uint32_t>(x) >= dst->width || static_cast<uint32_t>(y) >= dst->height ||
          static_cast<uint32_t>(sourceX) >= src->width || static_cast<uint32_t>(sourceY) >= src->height)
        continue;
      const uint32_t index = Vga_ReadPixel(srcBase, sourceX, sourceY);
      Vga_PlotPixel(dstBase, x, y, index);
    }
  }
  return kernel::KernelError::Success;
}

void Vga_Present(const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
                 const kernel::KObjectPtr<kernel::KSurface> &surface,
                 const kernel::KClipRect *rects, size_t count) {
  if (!surface || !surface->backing || !surface->backing->directAddress) return;
  const auto *context = adapter ? adapter->GetDeviceExtensionAs<VgaContext>() : nullptr;
  if (!context || !context->framebuffer) return;
  Vga_PresentShadow(surface->backing->directAddress.as_ptr<uint8_t>(),
                    context->framebuffer.as_ptr<volatile uint8_t>(), rects, count);
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

#undef VGA_MODE_FREE
#undef VGA_MODE_ALLOC
