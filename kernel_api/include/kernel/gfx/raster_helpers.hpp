#pragma once

#include <stdint.h>

namespace kernel::gfx {

struct BlitRect {
  int64_t dstX, dstY, srcX, srcY, width, height;
};

// Clip both surfaces in lockstep, preserving the source-to-destination offset.
inline bool ClipBlit(BlitRect &r, uint32_t dstWidth, uint32_t dstHeight,
                     uint32_t srcWidth, uint32_t srcHeight) {
  if (r.dstX < 0) { const auto n = -r.dstX; r.dstX = 0; r.srcX += n; r.width -= n; }
  if (r.dstY < 0) { const auto n = -r.dstY; r.dstY = 0; r.srcY += n; r.height -= n; }
  if (r.srcX < 0) { const auto n = -r.srcX; r.srcX = 0; r.dstX += n; r.width -= n; }
  if (r.srcY < 0) { const auto n = -r.srcY; r.srcY = 0; r.dstY += n; r.height -= n; }
  if (r.width > static_cast<int64_t>(dstWidth) - r.dstX) r.width = static_cast<int64_t>(dstWidth) - r.dstX;
  if (r.height > static_cast<int64_t>(dstHeight) - r.dstY) r.height = static_cast<int64_t>(dstHeight) - r.dstY;
  if (r.width > static_cast<int64_t>(srcWidth) - r.srcX) r.width = static_cast<int64_t>(srcWidth) - r.srcX;
  if (r.height > static_cast<int64_t>(srcHeight) - r.srcY) r.height = static_cast<int64_t>(srcHeight) - r.srcY;
  return r.width > 0 && r.height > 0;
}

// Work is bounded by the visible dominant axis, even for extreme coordinates.
// The unsigned product fits because validated endpoints are signed 32-bit.
template<typename Plot>
bool ForEachLinePixel(int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                      uint32_t width, uint32_t height, Plot &&plot) {
  if (x0 < INT32_MIN || x0 > INT32_MAX || y0 < INT32_MIN || y0 > INT32_MAX ||
      x1 < INT32_MIN || x1 > INT32_MAX || y1 < INT32_MIN || y1 > INT32_MAX) return false;
  const uint64_t dx = static_cast<uint64_t>(x1 > x0 ? x1 - x0 : x0 - x1);
  const uint64_t dy = static_cast<uint64_t>(y1 > y0 ? y1 - y0 : y0 - y1);
  if (dx >= dy && dx) {
    int64_t first = x0 < x1 ? x0 : x1;
    int64_t last = x0 > x1 ? x0 : x1;
    if (first < 0) first = 0;
    if (last >= width) last = static_cast<int64_t>(width) - 1;
    for (int64_t x = first; x <= last; ++x) {
      const uint64_t distance = static_cast<uint64_t>(x > x0 ? x - x0 : x0 - x);
      const int64_t step = static_cast<int64_t>((distance * dy + dx / 2) / dx);
      const int64_t y = y0 + (y1 >= y0 ? step : -step);
      if (y >= 0 && y < height) plot(static_cast<int32_t>(x), static_cast<int32_t>(y));
    }
  } else {
    int64_t first = y0 < y1 ? y0 : y1;
    int64_t last = y0 > y1 ? y0 : y1;
    if (first < 0) first = 0;
    if (last >= height) last = static_cast<int64_t>(height) - 1;
    for (int64_t y = first; y <= last; ++y) {
      const uint64_t distance = static_cast<uint64_t>(y > y0 ? y - y0 : y0 - y);
      const int64_t step = dy ? static_cast<int64_t>((distance * dx + dy / 2) / dy) : 0;
      const int64_t x = x0 + (x1 >= x0 ? step : -step);
      if (x >= 0 && x < width) plot(static_cast<int32_t>(x), static_cast<int32_t>(y));
    }
  }
  return true;
}

} // namespace kernel::gfx
