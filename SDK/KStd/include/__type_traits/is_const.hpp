#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>

namespace kstd {

template<class _Tp>
struct is_const : bool_constant<__is_const(_Tp)> {};

template<class _Tp>
inline constexpr bool is_const_v = __is_const(_Tp);

}// namespace kstd
