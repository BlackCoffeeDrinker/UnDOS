
#pragma once
#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {
template<class _T1, class _T2>
struct is_convertible : integral_constant<bool, __is_convertible(_T1, _T2)> {};

template<class _From, class _To>
inline constexpr bool is_convertible_v = __is_convertible(_From, _To);

template<class _Tp, class _Up>
struct is_nothrow_convertible : bool_constant<__is_nothrow_convertible(_Tp, _Up)> {};

template<class _Tp, class _Up>
inline constexpr bool is_nothrow_convertible_v = __is_nothrow_convertible(_Tp, _Up);

}// namespace kstd
