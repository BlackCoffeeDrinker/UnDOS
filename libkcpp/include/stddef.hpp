
#pragma once
#include <__config.hpp>

namespace kstd {
typedef __SIZE_TYPE__ size_t;
using ptrdiff_t = decltype(static_cast<int *>(nullptr) - static_cast<int *>(nullptr));

constexpr inline bool is_constant_evaluated() noexcept {
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

}// namespace kstd

using size_t = kstd::size_t;
using ptrdiff_t = kstd::ptrdiff_t;
using uint16_t = __UINT16_TYPE__;
