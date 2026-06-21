
#pragma once

#include <__config.hpp>

namespace kstd {
template<typename T>
[[nodiscard]] constexpr inline const T &min(const T &A, const T &B) {
  if (B < A) return B;
  return A;
}
}// namespace kstd
