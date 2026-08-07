
#include "stdkrn.hpp"

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

// Raw pointer to the pixel data of `surface`, or nullptr if the surface
// isn't a valid, already-resident (non-demand-paged) backing.
uint8_t *SurfaceBase(const kernel::KObjectPtr<kernel::KSurface> &surface) {
  if (!surface || !surface->backing || !surface->backing->directAddress) {
    return nullptr;
  }
  return surface->backing->directAddress.as_ptr<uint8_t>();
}

uint8_t *PixelAddress(const kernel::KObjectPtr<kernel::KSurface> &surface, int x, int y) {
  uint8_t *base = SurfaceBase(surface);
  if (!base) {
    return nullptr;
  }
  return base + static_cast<size_t>(y) * surface->pitch + static_cast<size_t>(x) * BytesPerPixel(surface->pixelFormat);
}

void WritePixel(const kernel::KObjectPtr<kernel::KSurface> &surface, int x, int y, uint32_t colorIndex) {
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
  }
}

int Abs(int value) {
  return value < 0 ? -value : value;
}

}// namespace

UNDOS_KERNEL_API_DEF bool KE_GFX_SoftwareFillRect(
    const kernel::KObjectPtr<kernel::KSurface> &dst,
    int x, int y, int w, int h,
    uint32_t colorIndex) noexcept {
  if (!SurfaceBase(dst) || w <= 0 || h <= 0) {
    return false;
  }

  for (int row = y; row < y + h; ++row) {
    for (int col = x; col < x + w; ++col) {
      WritePixel(dst, col, row, colorIndex);
    }
  }
  return true;
}

UNDOS_KERNEL_API_DEF bool KE_GFX_SoftwareDrawLine(
    const kernel::KObjectPtr<kernel::KSurface> &dst,
    int x0, int y0, int x1, int y1,
    uint32_t colorIndex) noexcept {
  if (!SurfaceBase(dst)) {
    return false;
  }

  // Classic Bresenham line algorithm.
  const int dx = Abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -Abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    WritePixel(dst, x0, y0, colorIndex);
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

UNDOS_KERNEL_API_DEF bool KE_GFX_SoftwareBitBlt(
    const kernel::KObjectPtr<kernel::KSurface> &dst,
    int dstX, int dstY,
    const kernel::KObjectPtr<kernel::KSurface> &src,
    int srcX, int srcY,
    uint32_t width, uint32_t height,
    kernel::RasterOp rop) noexcept {
  if (rop != kernel::RasterOp::SRCCOPY) {
    // Only straight copy is implemented in the software fallback.
    return false;
  }

  if (width == 0 || height == 0) {
    return true;
  }

  // 1. Resolve raw surface memory base pointers once
  uint8_t *dstBase = SurfaceBase(dst);
  const uint8_t *srcBase = SurfaceBase(src);

  if (!dstBase || !srcBase) {
    return false;
  }

  // 2. Cache invariant parameters locally
  const uint32_t dstPitch = dst->pitch;
  const uint32_t srcPitch = src->pitch;
  const kernel::PixelFormat dstFmt = dst->pixelFormat;
  const kernel::PixelFormat srcFmt = src->pixelFormat;
  const size_t dstBpp = BytesPerPixel(dstFmt);
  const size_t srcBpp = BytesPerPixel(srcFmt);

  // Calculate starting byte offsets for the top-left of the blit region
  uint8_t *dstRow = dstBase + static_cast<size_t>(dstY) * dstPitch + static_cast<size_t>(dstX) * dstBpp;
  const uint8_t *srcRow = srcBase + static_cast<size_t>(srcY) * srcPitch + static_cast<size_t>(srcX) * srcBpp;

  // --------------------------------------------------------------------------
  // FAST PATH: Matching Pixel Formats (Direct Memory Copy)
  // --------------------------------------------------------------------------
  if (dstFmt == srcFmt) {
    const size_t bytesPerRow = width * dstBpp;

    // Check if both source and destination rows are completely contiguous in memory
    if (bytesPerRow == dstPitch && bytesPerRow == srcPitch) {
      // Single bulk transfer for the entire region
      kstd::memmove(dstRow, srcRow, bytesPerRow * height);
      return true;
    }

    // Row-by-row memory copy
    for (uint32_t row = 0; row < height; ++row) {
      kstd::memmove(dstRow, srcRow, bytesPerRow);
      dstRow += dstPitch;
      srcRow += srcPitch;
    }
    return true;
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
          pixelValue = srcPixel[0];
          break;
        case kernel::PixelFormat::RGB24:
          pixelValue = static_cast<uint32_t>(srcPixel[0]) |
                       (static_cast<uint32_t>(srcPixel[1]) << 8) |
                       (static_cast<uint32_t>(srcPixel[2]) << 16);
          break;
        case kernel::PixelFormat::ARGB32:
          pixelValue = *reinterpret_cast<const uint32_t *>(srcPixel);
          break;
      }

      // Inline pixel write based on dstFmt
      switch (dstFmt) {
        case kernel::PixelFormat::INDEX8:
          dstPixel[0] = static_cast<uint8_t>(pixelValue);
          break;
        case kernel::PixelFormat::RGB24:
          dstPixel[0] = static_cast<uint8_t>(pixelValue & 0xFF);
          dstPixel[1] = static_cast<uint8_t>((pixelValue >> 8) & 0xFF);
          dstPixel[2] = static_cast<uint8_t>((pixelValue >> 16) & 0xFF);
          break;
        case kernel::PixelFormat::ARGB32:
          *reinterpret_cast<uint32_t *>(dstPixel) = pixelValue;
          break;
      }

      srcPixel += srcBpp;
      dstPixel += dstBpp;
    }

    dstRow += dstPitch;
    srcRow += srcPitch;
  }

  return true;
}
