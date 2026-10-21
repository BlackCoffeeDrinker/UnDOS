#pragma once

#include <__config.hpp>
#include <__fwd/tuple.hpp>

namespace kstd {

template<class, class>
struct pair;

template<class _Type>
inline const bool __is_pair_v = false;

template<class _Type1, class _Type2>
inline const bool __is_pair_v<pair<_Type1, _Type2>> = true;

template<size_t _Ip, class _T1, class _T2>
constexpr typename tuple_element<_Ip, pair<_T1, _T2>>::type &
get(pair<_T1, _T2> &) noexcept;

template<size_t _Ip, class _T1, class _T2>
constexpr const typename tuple_element<_Ip, pair<_T1, _T2>>::type &
get(const pair<_T1, _T2> &) noexcept;

#ifndef _LIBCPP_CXX03_LANG
template<size_t _Ip, class _T1, class _T2>
constexpr typename tuple_element<_Ip, pair<_T1, _T2>>::type &&
get(pair<_T1, _T2> &&) noexcept;

template<size_t _Ip, class _T1, class _T2>
constexpr const typename tuple_element<_Ip, pair<_T1, _T2>>::type &&
get(const pair<_T1, _T2> &&) noexcept;
#endif

}// namespace kstd
