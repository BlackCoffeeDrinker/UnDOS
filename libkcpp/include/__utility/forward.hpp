
#pragma once
#include <__config.hpp>

#include <__type_traits/is_reference.hpp>
#include <__type_traits/remove_reference.hpp>

#if __has_cpp_attribute(_Clang::__lifetimebound__)
#define _LIFETIMEBOUND [[_Clang::__lifetimebound__]]
#else
#define _LIFETIMEBOUND
#endif

namespace kstd {
template<class Tp>
[[nodiscard]] inline constexpr Tp &&
forward(_LIFETIMEBOUND remove_reference_t<Tp> &__t) noexcept {
  return static_cast<Tp &&>(__t);
}

template<class Tp>
[[nodiscard]] inline constexpr Tp &&
forward(_LIFETIMEBOUND remove_reference_t<Tp> &&_t) noexcept {
  static_assert(!is_lvalue_reference<Tp>::value, "cannot forward an rvalue as an lvalue");
  return static_cast<Tp &&>(_t);
}
}// namespace kstd
