
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {

template<class _Tp>
struct is_function : integral_constant<bool, __is_function(_Tp)> {};

template<class _Tp>
inline constexpr bool is_function_v = __is_function(_Tp);

}// namespace kstd
