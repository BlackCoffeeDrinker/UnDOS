
#include "stdkrn.hpp"
#include "../user/bitmap_font.hpp"
#include <kernel/gfx/raster_helpers.hpp>

namespace {
size_t BytesPerPixel(kernel::PixelFormat format) {
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

// Raw pointer to the pixel data of `surface`, or nullptr if the surface
// isn't a valid, already-resident (non-demand-paged) backing.
uint8_t *SurfaceBase(const kernel::KObjectPtr<kernel::KSurface> &surface) {
  if (!surface || !surface->backing || !surface->backing->directAddress) {
    return nullptr;
  }
  if (static_cast<uint32_t>(surface->pixelFormat) > static_cast<uint32_t>(kernel::PixelFormat::RGB16)) return nullptr;
  // A planar framebuffer (notably VGA mode12) is not INDEX8 packed storage.
  if (surface->type == kernel::SurfaceType::Framebuffer &&
      !(surface->flags & kernel::SurfaceFlags::Linear)) return nullptr;
  const int64_t pitch = surface->pitch < 0 ? -static_cast<int64_t>(surface->pitch) : surface->pitch;
  if (pitch < static_cast<int64_t>(surface->width) * static_cast<int64_t>(BytesPerPixel(surface->pixelFormat))) return nullptr;
  const uint64_t required = static_cast<uint64_t>(pitch) * surface->height;
  if (required > surface->backing->size || required > PTRDIFF_MAX) return nullptr;
  return surface->backing->directAddress.as_ptr<uint8_t>();
}

uint8_t *PixelAddress(const kernel::KObjectPtr<kernel::KSurface> &surface, int x, int y) {
  uint8_t *base = SurfaceBase(surface);
  if (!base) {
    return nullptr;
  }
  // KSurface::pitch is the signed Win2k lDelta, so the row offset is computed
  // in signed pointer arithmetic to keep a bottom-up surface addressable.
  return base + static_cast<ptrdiff_t>(y) * surface->pitch +
         static_cast<ptrdiff_t>(x) * static_cast<ptrdiff_t>(BytesPerPixel(surface->pixelFormat));
}

void WritePixel(const kernel::KObjectPtr<kernel::KSurface> &surface, int x, int y, uint32_t colorIndex) {
  if (!surface || x < 0 || y < 0 || static_cast<uint32_t>(x) >= surface->width ||
      static_cast<uint32_t>(y) >= surface->height) return;
  uint8_t *pixel = PixelAddress(surface, x, y);
  if (!pixel) {
    return;
  }
  switch (surface->pixelFormat) {
    case kernel::PixelFormat::INDEX8:
      pixel[0] = static_cast<uint8_t>(colorIndex);
      break;
    case kernel::PixelFormat::RGB24:
      pixel[0] = static_cast<uint8_t>(colorIndex & 0xFF);
      pixel[1] = static_cast<uint8_t>((colorIndex >> 8) & 0xFF);
      pixel[2] = static_cast<uint8_t>((colorIndex >> 16) & 0xFF);
      break;
    case kernel::PixelFormat::ARGB32:
      *reinterpret_cast<uint32_t *>(pixel) = colorIndex;
      break;
    case kernel::PixelFormat::RGB15:
    case kernel::PixelFormat::RGB16:
      *reinterpret_cast<uint16_t *>(pixel) = static_cast<uint16_t>(colorIndex);
      break;
  }
}

}// namespace

UNDOS_KERNEL_API_DEF kstd::error_code KE_GFX_SoftwareFillRect(
    const kernel::KObjectPtr<kernel::KSurface> &dst,
    int x, int y, int w, int h,
    uint32_t colorIndex) noexcept {
  auto *base = SurfaceBase(dst);
  if (!base) {
    return kernel::KernelError::Unsupported;
  }
  if (w <= 0 || h <= 0) {
    return kernel::KernelError::InvalidSize;
  }

  int64_t right = static_cast<int64_t>(x) + w;
  int64_t bottom = static_cast<int64_t>(y) + h;
  if (right > dst->width) right = dst->width;
  if (bottom > dst->height) bottom = dst->height;
  const int64_t left = x < 0 ? 0 : x;
  const int64_t top = y < 0 ? 0 : y;
  if (right <= left || bottom <= top) return kernel::KernelError::Success;
  const size_t bytes = BytesPerPixel(dst->pixelFormat);
  const uint8_t pattern[4] = {
      static_cast<uint8_t>(colorIndex), static_cast<uint8_t>(colorIndex >> 8),
      static_cast<uint8_t>(colorIndex >> 16), static_cast<uint8_t>(colorIndex >> 24)};
  for (int64_t row = top; row < bottom; ++row) {
    auto *pixel = base + static_cast<ptrdiff_t>(row) * dst->pitch +
                  static_cast<ptrdiff_t>(left) * static_cast<ptrdiff_t>(bytes);
    if (bytes == 1) {
      __builtin_memset(pixel, pattern[0], static_cast<size_t>(right - left));
      continue;
    }
    for (int64_t col = left; col < right; ++col) {
      for (size_t byte = 0; byte < bytes; ++byte) pixel[byte] = pattern[byte];
      pixel += bytes;
    }
  }
  return kernel::KernelError::Success;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_GFX_SoftwareDrawLine(
    const kernel::KObjectPtr<kernel::KSurface> &dst,
    int x0, int y0, int x1, int y1,
    uint32_t colorIndex) noexcept {
  if (!SurfaceBase(dst)) {
    return kernel::KernelError::Unsupported;
  }

  return kernel::gfx::ForEachLinePixel(x0, y0, x1, y1, dst->width, dst->height,
      [&](int32_t x, int32_t y) { WritePixel(dst, x, y, colorIndex); });
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_GFX_SoftwareBitBlt(
    const kernel::KObjectPtr<kernel::KSurface> &dst,
    int dstX, int dstY,
    const kernel::KObjectPtr<kernel::KSurface> &src,
    int srcX, int srcY,
    uint32_t width, uint32_t height,
    kernel::RasterOp rop) noexcept {
  if (rop != kernel::RasterOp::SRCCOPY) {
    // Only straight copy is implemented in the software fallback.
    return kernel::KernelError::Unsupported;
  }

  if (width == 0 || height == 0) {
    return kernel::KernelError::Success;
  }

  // 1. Resolve raw surface memory base pointers once
  uint8_t *dstBase = SurfaceBase(dst);
  const uint8_t *srcBase = SurfaceBase(src);

  if (!dstBase || !srcBase) {
    return kernel::KernelError::Unsupported;
  }

  kernel::gfx::BlitRect copy{dstX, dstY, srcX, srcY, width, height};
  if (!kernel::gfx::ClipBlit(copy, dst->width, dst->height, src->width, src->height)) return kernel::KernelError::Success;
  dstX = static_cast<int>(copy.dstX);
  dstY = static_cast<int>(copy.dstY);
  srcX = static_cast<int>(copy.srcX);
  srcY = static_cast<int>(copy.srcY);
  width = static_cast<uint32_t>(copy.width);
  height = static_cast<uint32_t>(copy.height);

  // 2. Cache invariant parameters locally
  const int32_t dstPitch = dst->pitch;
  const int32_t srcPitch = src->pitch;
  const kernel::PixelFormat dstFmt = dst->pixelFormat;
  const kernel::PixelFormat srcFmt = src->pixelFormat;
  const size_t dstBpp = BytesPerPixel(dstFmt);
  const size_t srcBpp = BytesPerPixel(srcFmt);
  const bool src332 = srcFmt == kernel::PixelFormat::INDEX8 && src->type == kernel::SurfaceType::Framebuffer;
  const bool dst332 = dstFmt == kernel::PixelFormat::INDEX8 && dst->type == kernel::SurfaceType::Framebuffer;
  const bool sharedBacking = dst->backing == src->backing;
  if (sharedBacking && (dstPitch != srcPitch || dstFmt != srcFmt)) return kernel::KernelError::Unsupported;

  // Calculate starting byte offsets for the top-left of the blit region
  uint8_t *dstRow = dstBase + static_cast<ptrdiff_t>(dstY) * dstPitch +
                    static_cast<ptrdiff_t>(dstX) * static_cast<ptrdiff_t>(dstBpp);
  const uint8_t *srcRow = srcBase + static_cast<ptrdiff_t>(srcY) * srcPitch +
                          static_cast<ptrdiff_t>(srcX) * static_cast<ptrdiff_t>(srcBpp);

  // --------------------------------------------------------------------------
  // FAST PATH: Matching Pixel Formats (Direct Memory Copy)
  // --------------------------------------------------------------------------
  if (dstFmt == srcFmt && src332 == dst332) {
    const size_t bytesPerRow = width * dstBpp;

    // Check if both source and destination rows are completely contiguous in
    // memory. A negative (bottom-up) pitch is never contiguous.
    if (dstPitch > 0 && srcPitch > 0 &&
        bytesPerRow == static_cast<size_t>(dstPitch) &&
        bytesPerRow == static_cast<size_t>(srcPitch)) {
      // Single bulk transfer for the entire region
      kstd::memmove(dstRow, srcRow, bytesPerRow * height);
      return kernel::KernelError::Success;
    }

    // Bottom-up logical traversal protects vertically overlapping rows.
    if (sharedBacking && dstY > srcY) {
      dstRow += static_cast<ptrdiff_t>(height - 1) * dstPitch;
      srcRow += static_cast<ptrdiff_t>(height - 1) * srcPitch;
      for (uint32_t row = 0; row < height; ++row) {
        kstd::memmove(dstRow, srcRow, bytesPerRow);
        dstRow -= dstPitch;
        srcRow -= srcPitch;
      }
      return kernel::KernelError::Success;
    }

    // Row-by-row memory copy, including horizontal overlap.
    for (uint32_t row = 0; row < height; ++row) {
      kstd::memmove(dstRow, srcRow, bytesPerRow);
      dstRow += dstPitch;
      srcRow += srcPitch;
    }
    return kernel::KernelError::Success;
  }

  // --------------------------------------------------------------------------
  // SLOW PATH: Format Conversion (Hoisted Branches)
  // --------------------------------------------------------------------------
  for (uint32_t row = 0; row < height; ++row) {
    const uint8_t *srcPixel = srcRow;
    uint8_t *dstPixel = dstRow;

    for (uint32_t col = 0; col < width; ++col) {
      // Inline pixel read based on srcFmt
      uint32_t pixelValue = 0;
      switch (srcFmt) {
        case kernel::PixelFormat::INDEX8:
          if (src332) {
            const uint32_t r = (srcPixel[0] >> 5) & 7;
            const uint32_t g = (srcPixel[0] >> 2) & 7;
            const uint32_t b = srcPixel[0] & 3;
            pixelValue = ((r * 255 / 7) << 16) | ((g * 255 / 7) << 8) | (b * 255 / 3);
          } else pixelValue = kernel::gfx::Palette16Rgb(srcPixel[0]);
          break;
        case kernel::PixelFormat::RGB24:
          pixelValue = static_cast<uint32_t>(srcPixel[0]) |
                       (static_cast<uint32_t>(srcPixel[1]) << 8) |
                       (static_cast<uint32_t>(srcPixel[2]) << 16);
          break;
        case kernel::PixelFormat::ARGB32:
          pixelValue = *reinterpret_cast<const uint32_t *>(srcPixel) & 0xffffffu;
          break;
        case kernel::PixelFormat::RGB15: {
          const uint32_t value = *reinterpret_cast<const uint16_t *>(srcPixel);
          const uint32_t r = (value >> 10) & 31;
          const uint32_t g = (value >> 5) & 31;
          const uint32_t b = value & 31;
          pixelValue = (((r << 3) | (r >> 2)) << 16) |
                       (((g << 3) | (g >> 2)) << 8) | (b << 3) | (b >> 2);
          break;
        }
        case kernel::PixelFormat::RGB16: {
          const uint32_t value = *reinterpret_cast<const uint16_t *>(srcPixel);
          const uint32_t r = (value >> 11) & 31;
          const uint32_t g = (value >> 5) & 63;
          const uint32_t b = value & 31;
          pixelValue = (((r << 3) | (r >> 2)) << 16) |
                       (((g << 2) | (g >> 4)) << 8) | (b << 3) | (b >> 2);
          break;
        }
      }

      // Inline pixel write based on dstFmt
      switch (dstFmt) {
        case kernel::PixelFormat::INDEX8:
          dstPixel[0] = static_cast<uint8_t>(dst332 ? kernel::gfx::RgbTo332(pixelValue) : kernel::gfx::RgbToPalette16(pixelValue));
          break;
        case kernel::PixelFormat::RGB24:
          dstPixel[0] = static_cast<uint8_t>(pixelValue & 0xFF);
          dstPixel[1] = static_cast<uint8_t>((pixelValue >> 8) & 0xFF);
          dstPixel[2] = static_cast<uint8_t>((pixelValue >> 16) & 0xFF);
          break;
        case kernel::PixelFormat::ARGB32:
          *reinterpret_cast<uint32_t *>(dstPixel) = 0xff000000u | pixelValue;
          break;
        case kernel::PixelFormat::RGB15:
          *reinterpret_cast<uint16_t *>(dstPixel) = static_cast<uint16_t>(kernel::gfx::RgbTo555(pixelValue));
          break;
        case kernel::PixelFormat::RGB16:
          *reinterpret_cast<uint16_t *>(dstPixel) = static_cast<uint16_t>(kernel::gfx::RgbTo565(pixelValue));
          break;
      }

      srcPixel += srcBpp;
      dstPixel += dstBpp;
    }

    dstRow += dstPitch;
    srcRow += srcPitch;
  }

  return kernel::KernelError::Success;
}
