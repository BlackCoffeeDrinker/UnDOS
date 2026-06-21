
#pragma once
#include <__config.hpp>

#include <__type_traits/bool_constant.hpp>

namespace kstd {
template<class _Tp, class _Up>
struct is_assignable : bool_constant<__is_assignable(_Tp, _Up)> {};

template<class _Tp, class _Arg>
inline constexpr bool is_assignable_v = __is_assignable(_Tp, _Arg);

template<class _Tp>
struct is_copy_assignable
    : integral_constant<bool, __is_assignable(__add_lvalue_reference_t<_Tp>, __add_lvalue_reference_t<const _Tp>)> {};

template<class _Tp>
inline constexpr bool is_copy_assignable_v = is_copy_assignable<_Tp>::value;

template<class _Tp>
struct is_move_assignable
    : integral_constant<bool, __is_assignable(__add_lvalue_reference_t<_Tp>, __add_rvalue_reference_t<_Tp>)> {};

template<class _Tp>
inline constexpr bool is_move_assignable_v = is_move_assignable<_Tp>::value;
}// namespace kstd
