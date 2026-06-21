
#pragma once
#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/add_reference.hpp>

namespace kstd {
template<class _Tp, class _Arg>
struct is_nothrow_assignable : integral_constant<bool, __is_nothrow_assignable(_Tp, _Arg)> {
};

template<class _Tp, class _Arg>
inline constexpr bool is_nothrow_assignable_v = __is_nothrow_assignable(_Tp, _Arg);

template<class _Tp>
struct is_nothrow_copy_assignable
    : integral_constant<bool,
                        __is_nothrow_assignable(add_lvalue_reference_t<_Tp>, add_lvalue_reference_t<const _Tp>)> {};

template<class _Tp>
inline constexpr bool is_nothrow_copy_assignable_v = is_nothrow_copy_assignable<_Tp>::value;

template<class _Tp>
struct is_nothrow_move_assignable
    : integral_constant<bool, __is_nothrow_assignable(__add_lvalue_reference_t<_Tp>, __add_rvalue_reference_t<_Tp>)> {};

template<class _Tp>
inline constexpr bool is_nothrow_move_assignable_v = is_nothrow_move_assignable<_Tp>::value;

}// namespace kstd
