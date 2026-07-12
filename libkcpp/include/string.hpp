
#pragma once

#include <__config.hpp>

namespace kstd {

_KSTD_API inline void *memcpy(void *dst, const void *src, size_t n) {
  auto *d = static_cast<uint8_t *>(dst);
  const auto *s = static_cast<const uint8_t *>(src);

  // Check if both pointers are 4-byte aligned and we have enough bytes
  if ((reinterpret_cast<uintptr_t>(d) & 3) == 0 &&
      (reinterpret_cast<uintptr_t>(s) & 3) == 0 &&
      n >= 4) {
    // Copy 4 bytes at a time
    auto *d32 = reinterpret_cast<uint32_t *>(d);
    const auto *s32 = reinterpret_cast<const uint32_t *>(s);
    const size_t count = n >> 2;

    for (size_t i = 0; i < count; ++i) {
      d32[i] = s32[i];
    }

    // Update pointers and remaining byte count
    d += count * 4;
    s += count * 4;
    n -= count * 4;
  }

  // Copy remaining bytes
  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }

  return dst;
}

}// namespace kstd
