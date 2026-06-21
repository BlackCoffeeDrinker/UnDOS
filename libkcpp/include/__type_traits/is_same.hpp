
#pragma once
#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>

namespace kstd {

template<class _Tp, class _Up>
struct is_same : bool_constant<__is_same(_Tp, _Up)> {};

#if __has_builtin(__is_same)
template<typename _Tp, typename _Up>
inline constexpr bool is_same_v = __is_same(_Tp, _Up);
#else
template<typename _Tp, typename _Up>
inline constexpr bool is_same_v = false;
template<typename _Tp>
inline constexpr bool is_same_v<_Tp, _Tp> = true;
#endif

template<class _Tp, class _Up>
using _IsSame = bool_constant<__is_same(_Tp, _Up)>;

template<class _Tp, class _Up>
using _IsNotSame = bool_constant<!__is_same(_Tp, _Up)>;


}// namespace kstd
