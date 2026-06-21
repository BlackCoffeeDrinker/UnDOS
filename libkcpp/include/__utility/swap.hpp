
#pragma once

#include <__config.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_assignable.hpp>
#include <__type_traits/is_constructible.hpp>
#include <__type_traits/is_nothrow_assignable.hpp>
#include <__type_traits/is_nothrow_constructible.hpp>
#include <__type_traits/is_swappable.hpp>
#include <__utility/move.hpp>

namespace kstd {
template<class _Tp>
using __swap_result_t =
    enable_if_t<is_move_constructible<_Tp>::value && is_move_assignable<_Tp>::value>;

template<class _Tp>
inline __swap_result_t<_Tp> constexpr swap(_Tp &__x, _Tp &__y) noexcept(is_nothrow_move_constructible<_Tp>::value && is_nothrow_move_assignable<_Tp>::value) {
  _Tp __t(kstd::move(__x));
  __x = kstd::move(__y);
  __y = kstd::move(__t);
}

template<class _Tp, size_t _Np, enable_if_t<__is_swappable_v<_Tp>, int>>
inline constexpr void swap(_Tp (&__a)[_Np], _Tp (&__b)[_Np]) noexcept(__is_nothrow_swappable_v<_Tp>) {
  for (size_t __i = 0; __i != _Np; ++__i) {
    swap(__a[__i], __b[__i]);
  }
}
}// namespace kstd
