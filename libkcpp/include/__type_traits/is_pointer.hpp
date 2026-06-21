
#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {

template<class _Tp>
struct is_pointer : bool_constant<__is_pointer(_Tp)> {};

template<class _Tp>
inline constexpr bool is_pointer_v = __is_pointer(_Tp);

}// namespace kstd
