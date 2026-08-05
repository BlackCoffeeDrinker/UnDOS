
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

uint32_t ReadPixel(const kernel::KObjectPtr<kernel::KSurface> &surface, int x, int y) {
  uint8_t *pixel = PixelAddress(surface, x, y);
  if (!pixel) {
    return 0;
  }
  switch (surface->pixelFormat) {
    case kernel::PixelFormat::INDEX8:
      return pixel[0];
    case kernel::PixelFormat::RGB24:
      return static_cast<uint32_t>(pixel[0]) | (static_cast<uint32_t>(pixel[1]) << 8) | (static_cast<uint32_t>(pixel[2]) << 16);
    case kernel::PixelFormat::ARGB32:
      return *reinterpret_cast<uint32_t *>(pixel);
  }
  return 0;
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
    // Only a straight copy is implemented in the software fallback.
    return false;
  }

  if (!SurfaceBase(dst) || !SurfaceBase(src)) {
    return false;
  }

  for (uint32_t row = 0; row < height; ++row) {
    for (uint32_t col = 0; col < width; ++col) {
      const uint32_t pixel = ReadPixel(src, srcX + static_cast<int>(col), srcY + static_cast<int>(row));
      WritePixel(dst, dstX + static_cast<int>(col), dstY + static_cast<int>(row), pixel);
    }
  }
  return true;
}
