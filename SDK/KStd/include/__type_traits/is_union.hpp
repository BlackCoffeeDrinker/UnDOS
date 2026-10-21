
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {
template<class _Tp>
struct is_union : integral_constant<bool, __is_union(_Tp)> {};

template<class _Tp>
inline constexpr bool is_union_v = __is_union(_Tp);
}// namespace kstd
