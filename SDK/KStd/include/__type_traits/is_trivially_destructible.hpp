
#pragma once

#include <__config.hpp>

#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_destructible.hpp>

namespace kstd {

#if __has_builtin(__is_trivially_destructible)

template<class _Tp>
struct is_trivially_destructible
    : integral_constant<bool, __is_trivially_destructible(_Tp)> {};

#elif __has_builtin(__has_trivial_destructor)

template<class _Tp>
struct is_trivially_destructible
    : integral_constant<bool, is_destructible<_Tp>::value &&__has_trivial_destructor(_Tp)> {};

#else

#error is_trivially_destructible is not implemented

#endif// __has_builtin(__is_trivially_destructible)

template<class _Tp>
inline constexpr bool is_trivially_destructible_v = is_trivially_destructible<_Tp>::value;

}// namespace kstd
