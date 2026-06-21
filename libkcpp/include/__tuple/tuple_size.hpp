
#pragma once

#include <__config.hpp>
#include <__fwd/tuple.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_const.hpp>
#include <__type_traits/is_volatile.hpp>

namespace kstd {
template<class _Tp>
struct tuple_size;

template<class _Tp, class...>
using __enable_if_tuple_size_imp = _Tp;

template<class _Tp>
struct tuple_size<
    __enable_if_tuple_size_imp<const _Tp, enable_if_t<!is_volatile<_Tp>::value>, decltype(tuple_size<_Tp>::value)>>
    : public integral_constant<size_t, tuple_size<_Tp>::value> {};

template<class _Tp>
struct tuple_size<
    __enable_if_tuple_size_imp<volatile _Tp, enable_if_t<!is_const<_Tp>::value>, decltype(tuple_size<_Tp>::value)>>
    : public integral_constant<size_t, tuple_size<_Tp>::value> {};

template<class _Tp>
struct tuple_size<__enable_if_tuple_size_imp<const volatile _Tp, decltype(tuple_size<_Tp>::value)>>
    : public integral_constant<size_t, tuple_size<_Tp>::value> {};

template<class... _Tp>
struct tuple_size<tuple<_Tp...>> : public integral_constant<size_t, sizeof...(_Tp)> {};

template<class _Tp>
inline constexpr size_t tuple_size_v = tuple_size<_Tp>::value;

}// namespace kstd
