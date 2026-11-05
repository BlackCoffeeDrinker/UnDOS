
#pragma once

#include <__config.hpp>

namespace kstd {

_KSTD_API constexpr inline bool is_constant_evaluated() noexcept {
#if __cpp_if_consteval >= 202106L
  if consteval {
    return true;
  } else {
    return false;
  }
#elif __cplusplus >= 201103L && __has_builtin(__builtin_is_constant_evaluated)
  return __builtin_is_constant_evaluated();
#else
  static_assert(false, "is_constant_evaluated is not supported.");
  return false;
#endif
}
}
