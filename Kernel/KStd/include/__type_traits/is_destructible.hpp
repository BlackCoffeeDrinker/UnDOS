
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_function.hpp>
#include <__type_traits/is_reference.hpp>
#include <__type_traits/remove_all_extents.hpp>
#include <__utility/declval.hpp>

namespace kstd {

#if __has_builtin(__is_destructible)

template<class _Tp>
struct is_destructible : bool_constant<__is_destructible(_Tp)> {};

template<class _Tp>
inline constexpr bool is_destructible_v = __is_destructible(_Tp);

#else// __has_builtin(__is_destructible)

//  if it's a reference, return true
//  if it's a function, return false
//  if it's   void,     return false
//  if it's an array of unknown bound, return false
//  Otherwise, return "declval<_Up&>().~_Up()" is well-formed
//    where _Up is remove_all_extents<_Tp>::type

template<class>
struct __is_destructible_apply {
  typedef int type;
};

template<typename _Tp>
struct __is_destructor_wellformed {
  template<typename _Tp1>
  static true_type __test(typename __is_destructible_apply<decltype(kstd::declval<_Tp1 &>().~_Tp1())>::type);

  template<typename _Tp1>
  static false_type __test(...);

  static const bool value = decltype(__test<_Tp>(12))::value;
};

template<class _Tp, bool>
struct __destructible_imp;

template<class _Tp>
struct __destructible_imp<_Tp, false>
    : integral_constant<bool, __is_destructor_wellformed<__remove_all_extents_t<_Tp>>::value> {};

template<class _Tp>
struct __destructible_imp<_Tp, true> : true_type {};

template<class _Tp, bool>
struct __destructible_false;

template<class _Tp>
struct __destructible_false<_Tp, false> : __destructible_imp<_Tp, is_reference<_Tp>::value> {};

template<class _Tp>
struct __destructible_false<_Tp, true> : false_type {};

template<class _Tp>
struct is_destructible : __destructible_false<_Tp, is_function<_Tp>::value> {};

template<class _Tp>
struct is_destructible<_Tp[]> : false_type {};

template<>
struct is_destructible<void> : false_type {};

template<class _Tp>
inline constexpr bool is_destructible_v = is_destructible<_Tp>::value;
#endif

}// namespace kstd
