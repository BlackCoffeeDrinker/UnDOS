
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {

template<class _Tp>
struct is_class : integral_constant<bool, __is_class(_Tp)> {};

template<class _Tp>
inline constexpr bool is_class_v = __is_class(_Tp);

}// namespace kstd
