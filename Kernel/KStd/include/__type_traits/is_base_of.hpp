
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {

template<class _Bp, class _Dp>
struct is_base_of : integral_constant<bool, __is_base_of(_Bp, _Dp)> {};

template<class _Bp, class _Dp>
inline constexpr bool is_base_of_v = __is_base_of(_Bp, _Dp);

}// namespace kstd
