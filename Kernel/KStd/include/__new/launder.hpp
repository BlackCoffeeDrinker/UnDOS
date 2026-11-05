
#pragma once

#include <__config.hpp>

namespace kstd {

template <class _Tp>
[[__nodiscard__]] inline _KSTD_API constexpr _Tp* __launder(_Tp* __p) noexcept {
  // The compiler diagnoses misuses of __builtin_launder, so we don't need to add any static_asserts
  // to implement the Mandates.
  return __builtin_launder(__p);
}


template <class _Tp>
[[nodiscard]] inline _KSTD_API constexpr _Tp* launder(_Tp* __p) noexcept {
  return __builtin_launder(__p);
}

}
