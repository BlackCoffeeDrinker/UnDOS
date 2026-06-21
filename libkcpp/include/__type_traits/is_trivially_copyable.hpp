
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {

template<class _Tp>
struct is_trivially_copyable : integral_constant<bool, __is_trivially_copyable(_Tp)> {};

template<class _Tp>
inline constexpr bool is_trivially_copyable_v = __is_trivially_copyable(_Tp);

template<class _Tp>
inline const bool __is_cheap_to_copy = __is_trivially_copyable(_Tp) && sizeof(_Tp) <= sizeof(intmax_t);

}// namespace kstd
