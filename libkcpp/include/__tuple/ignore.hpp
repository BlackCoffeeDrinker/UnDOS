
#pragma once

#include <__config.hpp>

namespace kstd {

struct __ignore_type {
  template<class _Tp>
  constexpr const __ignore_type &operator=(const _Tp &) const noexcept {
    return *this;
  }
};

inline constexpr __ignore_type ignore;

}// namespace kstd
