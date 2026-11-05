
#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>

namespace kstd {

template<class _Tp>
struct is_array : bool_constant<__is_array(_Tp)> {};

template<class _Tp>
inline constexpr bool is_array_v = __is_array(_Tp);

template<class _Tp>
inline const bool __is_bounded_array_v = __is_bounded_array(_Tp);


template<class _Tp>
struct is_bounded_array : bool_constant<__is_bounded_array(_Tp)> {};

template<class _Tp>
inline constexpr bool is_bounded_array_v = __is_bounded_array(_Tp);


template<class _Tp>
inline const bool __is_unbounded_array_v = __is_unbounded_array(_Tp);

template<class _Tp>
struct is_unbounded_array : bool_constant<__is_unbounded_array(_Tp)> {};

template<class _Tp>
inline constexpr bool is_unbounded_array_v = __is_unbounded_array(_Tp);

}// namespace kstd
