
#pragma once
#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/add_reference.hpp>

namespace kstd {
template<class _Tp, class... _Args>
struct is_constructible : integral_constant<bool, __is_constructible(_Tp, _Args...)> {};

template<class _Tp, class... _Args>
inline constexpr bool is_constructible_v = __is_constructible(_Tp, _Args...);

template<class _Tp>
struct is_copy_constructible
    : integral_constant<bool, __is_constructible(_Tp, __add_lvalue_reference_t<const _Tp>)> {};

template<class _Tp>
inline constexpr bool is_copy_constructible_v = is_copy_constructible<_Tp>::value;

template<class _Tp>
struct is_move_constructible
    : integral_constant<bool, __is_constructible(_Tp, __add_rvalue_reference_t<_Tp>)> {};

template<class _Tp>
inline constexpr bool is_move_constructible_v = is_move_constructible<_Tp>::value;

template<class _Tp>
struct is_default_constructible : integral_constant<bool, __is_constructible(_Tp)> {};

template<class _Tp>
inline constexpr bool is_default_constructible_v = __is_constructible(_Tp);

}// namespace kstd
