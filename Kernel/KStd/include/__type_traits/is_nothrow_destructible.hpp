
#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_destructible.hpp>
#include <__utility/declval.hpp>

namespace kstd {

#if __has_builtin(__is_nothrow_destructible)
template<class _Tp>
struct is_nothrow_destructible : integral_constant<bool, __is_nothrow_destructible(_Tp)> {};
#else
template<bool, class _Tp>
struct __internal_is_nothrow_destructible;

template<class _Tp>
struct __internal_is_nothrow_destructible<false, _Tp> : false_type {};

template<class _Tp>
struct __internal_is_nothrow_destructible<true, _Tp> : integral_constant<bool, noexcept(kstd::declval<_Tp>().~_Tp())> {};

template<class _Tp>
struct is_nothrow_destructible : __internal_is_nothrow_destructible<is_destructible<_Tp>::value, _Tp> {};

template<class _Tp, size_t _Ns>
struct is_nothrow_destructible<_Tp[_Ns]> : is_nothrow_destructible<_Tp> {};

template<class _Tp>
struct is_nothrow_destructible<_Tp &> : true_type {};

template<class _Tp>
struct is_nothrow_destructible<_Tp &&> : true_type {};

#endif// __has_builtin(__is_nothrow_destructible)

template<class _Tp>
inline constexpr bool is_nothrow_destructible_v = is_nothrow_destructible<_Tp>::value;
}// namespace kstd
