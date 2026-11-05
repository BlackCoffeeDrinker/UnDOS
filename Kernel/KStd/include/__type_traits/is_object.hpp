
#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>

namespace kstd {
template<class _Tp>
struct is_object : bool_constant<__is_object(_Tp)> {};

template<class _Tp>
inline constexpr bool is_object_v = __is_object(_Tp);

}// namespace kstd
