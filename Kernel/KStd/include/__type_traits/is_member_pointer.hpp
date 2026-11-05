
#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {

template<class _Tp>
struct is_member_pointer : bool_constant<__is_member_pointer(_Tp)> {};

template<class _Tp>
struct is_member_object_pointer : bool_constant<__is_member_object_pointer(_Tp)> {};

template<class _Tp>
struct is_member_function_pointer : bool_constant<__is_member_function_pointer(_Tp)> {};

template<class _Tp>
inline constexpr bool is_member_pointer_v = __is_member_pointer(_Tp);

template<class _Tp>
inline constexpr bool is_member_object_pointer_v = __is_member_object_pointer(_Tp);

template<class _Tp>
inline constexpr bool is_member_function_pointer_v = __is_member_function_pointer(_Tp);

}// namespace kstd
