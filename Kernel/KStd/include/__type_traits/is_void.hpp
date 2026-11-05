
#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {

template<class _Tp>
struct is_void : bool_constant<__is_same(__remove_cv(_Tp), void)> {};

template<class _Tp>
inline constexpr bool is_void_v = __is_same(__remove_cv(_Tp), void);

}// namespace kstd
