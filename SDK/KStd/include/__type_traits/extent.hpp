
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {
#if __has_builtin(__array_extent)
template<class _Tp, size_t _Dim = 0>
struct extent : integral_constant<size_t, __array_extent(_Tp, _Dim)> {};

template<class _Tp, unsigned _Ip = 0>
inline constexpr size_t extent_v = __array_extent(_Tp, _Ip);
#else// __has_builtin(__array_extent)
template<class _Tp, unsigned _Ip = 0>
struct extent : public integral_constant<size_t, 0> {};
template<class _Tp>
struct extent<_Tp[], 0> : public integral_constant<size_t, 0> {};
template<class _Tp, unsigned _Ip>
struct extent<_Tp[], _Ip> : public integral_constant<size_t, extent<_Tp, _Ip - 1>::value> {};
template<class _Tp, size_t _Np>
struct extent<_Tp[_Np], 0> : public integral_constant<size_t, _Np> {};
template<class _Tp, size_t _Np, unsigned _Ip>
struct extent<_Tp[_Np], _Ip> : public integral_constant<size_t, extent<_Tp, _Ip - 1>::value> {};

template<class _Tp, unsigned _Ip = 0>
inline constexpr size_t extent_v = extent<_Tp, _Ip>::value;

#endif// __has_builtin(__array_extent)

}// namespace kstd
