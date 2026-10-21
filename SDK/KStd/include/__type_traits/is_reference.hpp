
#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>

namespace kstd {

template<class Tp>
struct is_reference : bool_constant<__is_reference(Tp)> {};

template<class Tp>
inline constexpr bool is_reference_v = __is_reference(Tp);

#if __has_builtin(__is_lvalue_reference)
template<class _Tp>
struct is_lvalue_reference : bool_constant<__is_lvalue_reference(_Tp)> {};

template<class _Tp>
inline constexpr bool is_lvalue_reference_v = __is_lvalue_reference(_Tp);
#else
template<class T>
struct is_lvalue_reference : false_type {};
template<class T>
struct is_lvalue_reference<T &> : true_type {};

template<class T>
inline constexpr bool is_lvalue_reference_v = is_lvalue_reference<T>::value;
#endif


#if __has_builtin(__is_rvalue_reference)
template<class _Tp>
struct is_rvalue_reference : bool_constant<__is_rvalue_reference(_Tp)> {};

template<class _Tp>
inline constexpr bool is_rvalue_reference_v = __is_rvalue_reference(_Tp);
#else
template<class T>
struct is_rvalue_reference : false_type {};
template<class T>
struct is_rvalue_reference<T &&> : true_type {};
template<class T>
inline constexpr bool is_rvalue_reference_v = is_rvalue_reference<T>::value;
#endif

}// namespace kstd
