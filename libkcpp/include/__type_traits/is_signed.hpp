
#pragma once
#include <__config.hpp>

#include <__type_traits/bool_constant.hpp>
#include <__type_traits/is_arithmetic.hpp>

namespace kstd {
template<typename _Tp, bool = is_arithmetic<_Tp>::value>
struct is_signed : false_type {};

template<typename _Tp>
struct is_signed<_Tp, true> : bool_constant<_Tp(-1) < _Tp(0)> {};

template<typename _Tp>
inline constexpr bool is_signed_v = is_signed<_Tp>::value;
}// namespace kstd
