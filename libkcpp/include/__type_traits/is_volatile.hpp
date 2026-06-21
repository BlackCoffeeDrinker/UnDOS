
#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>

namespace kstd {
template<class _Tp>
struct is_volatile : bool_constant<__is_volatile(_Tp)> {};

template<class _Tp>
inline constexpr bool is_volatile_v = __is_volatile(_Tp);
}// namespace kstd
