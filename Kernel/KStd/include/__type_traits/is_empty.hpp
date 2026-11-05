
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>


namespace kstd {
template<class _Tp>
struct is_empty : integral_constant<bool, __is_empty(_Tp)> {};

template<class _Tp>
inline constexpr bool is_empty_v = __is_empty(_Tp);
}// namespace kstd
