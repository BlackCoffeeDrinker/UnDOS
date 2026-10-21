
#pragma once
#include <__config.hpp>

namespace kstd {
template<class _Tp, class... _Args>
struct is_nothrow_constructible
    : integral_constant<bool, __is_nothrow_constructible(_Tp, _Args...)> {};

template<class _Tp, class... _Args>
inline constexpr bool is_nothrow_constructible_v =
    is_nothrow_constructible<_Tp, _Args...>::value;

template<class _Tp>
struct is_nothrow_copy_constructible
    : integral_constant<bool, __is_nothrow_constructible(_Tp, __add_lvalue_reference_t<const _Tp>)> {};

template<class _Tp>
inline constexpr bool is_nothrow_copy_constructible_v =
    is_nothrow_copy_constructible<_Tp>::value;

template<class _Tp>
struct is_nothrow_move_constructible
    : integral_constant<bool, __is_nothrow_constructible(_Tp, __add_rvalue_reference_t<_Tp>)> {};

template<class _Tp>
inline constexpr bool is_nothrow_move_constructible_v =
    is_nothrow_move_constructible<_Tp>::value;

template<class _Tp>
struct is_nothrow_default_constructible
    : integral_constant<bool, __is_nothrow_constructible(_Tp)> {};

template<class _Tp>
inline constexpr bool is_nothrow_default_constructible_v = __is_nothrow_constructible(_Tp);

}// namespace kstd
