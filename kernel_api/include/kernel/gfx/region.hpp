#pragma once

#include <stdint.h>

namespace kernel::user {

// Half-open rectangle [left, right) x [top, bottom).
struct URect {
  int32_t left{0};
  int32_t top{0};
  int32_t right{0};
  int32_t bottom{0};

  [[nodiscard]] constexpr bool Empty() const noexcept { return right <= left || bottom <= top; }

  [[nodiscard]] constexpr URect Intersect(const URect &o) const noexcept {
    const URect r{
        .left = left > o.left ? left : o.left,
        .top = top > o.top ? top : o.top,
        .right = right < o.right ? right : o.right,
        .bottom = bottom < o.bottom ? bottom : o.bottom};

    if (r.Empty()) {
      return {};
    }

    return r;
  }

  [[nodiscard]] constexpr URect Union(const URect &o) const noexcept {
    if (Empty()) { return o; }
    if (o.Empty()) { return *this; }

    return {
        .left = left < o.left ? left : o.left,
        .top = top < o.top ? top : o.top,
        .right = right > o.right ? right : o.right,
        .bottom = bottom > o.bottom ? bottom : o.bottom};
  }

  [[nodiscard]] constexpr URect Offset(int32_t dx, int32_t dy) const noexcept {
    return {
        .left = left + dx,
        .top = top + dy,
        .right = right + dx,
        .bottom = bottom + dy};
  }

  constexpr bool operator==(const URect &) const noexcept = default;
};

// Fixed-capacity list of disjoint rectangles (no allocation). When a
// subtraction would exceed capacity, discard that fragment. Visibility clips
// must fail closed: drawing too little is preferable to corrupting an occluder.
struct URegion {
  static constexpr uint32_t kMaxRects = 32;

  URect rects[kMaxRects]{};
  uint32_t count{0};

  void Clear() noexcept { count = 0; }

  void Set(const URect &r) noexcept {
    count = 0;
    if (!r.Empty()) rects[count++] = r;
  }

  [[nodiscard]] bool Empty() const noexcept { return count == 0; }

  [[nodiscard]] URect Bounds() const noexcept {
    URect b{};
    for (uint32_t i = 0; i < count; ++i) {
      b = b.Union(rects[i]);
    }
    return b;
  }

  void Intersect(const URect &clip) noexcept {
    uint32_t out = 0;
    for (uint32_t i = 0; i < count; ++i) {
      const auto r = rects[i].Intersect(clip);
      if (!r.Empty()) {
        rects[out++] = r;
      }
    }
    count = out;
  }

  void Subtract(const URect &cut) noexcept {
    if (cut.Empty()) return;
    URect result[kMaxRects];
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; ++i) {
      const URect &r = rects[i];
      const URect x = r.Intersect(cut);
      if (x.Empty()) {
        if (n < kMaxRects) result[n++] = r;
        continue;
      }
      URect parts[4];
      uint32_t p = 0;
      
      if (r.top < x.top) parts[p++] = {.left = r.left, .top = r.top, .right = r.right, .bottom = x.top};
      if (x.bottom < r.bottom) parts[p++] = {.left = r.left, .top = x.bottom, .right = r.right, .bottom = r.bottom};
      if (r.left < x.left) parts[p++] = {.left = r.left, .top = x.top, .right = x.left, .bottom = x.bottom};
      if (x.right < r.right) parts[p++] = {.left = x.right, .top = x.top, .right = r.right, .bottom = x.bottom};
      
      if (n + p > kMaxRects) {
        continue;
      }
      for (uint32_t k = 0; k < p; ++k) result[n++] = parts[k];
    }
    for (uint32_t i = 0; i < n; ++i) rects[i] = result[i];
    count = n;
  }
};

// Presenting a fragmented region costs blitter setup per rectangle, so a
// region whose bounding box is barely larger than the pixels it covers is
// cheaper to transfer as one rectangle. True when the bounding box wastes
// less than a quarter of the covered area.
[[nodiscard]] inline bool region_merges_to_bounds(const URegion &region) noexcept {
  if (region.count < 2) return true;
  uint64_t covered = 0;
  
  for (uint32_t i = 0; i < region.count; ++i) {
    const auto &r = region.rects[i];
    covered += static_cast<uint64_t>(r.right - r.left) * static_cast<uint64_t>(r.bottom - r.top);
  }
  
  const auto b = region.Bounds();
  const uint64_t bounds = static_cast<uint64_t>(b.right - b.left) * static_cast<uint64_t>(b.bottom - b.top);
  return bounds <= covered + covered / 4;
}

}// namespace kernel::user
