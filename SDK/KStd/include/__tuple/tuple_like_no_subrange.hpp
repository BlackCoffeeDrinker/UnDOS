
#pragma once

#include <__config.hpp>
#include <__fwd/array.hpp>
#include <__fwd/complex.hpp>
#include <__fwd/pair.hpp>
#include <__fwd/tuple.hpp>
#include <__tuple/tuple_size.hpp>
#include <__type_traits/remove_cvref.hpp>

namespace kstd {

template<class _Tp>
inline constexpr bool __tuple_like_no_subrange_impl = false;

template<class... _Tp>
inline constexpr bool __tuple_like_no_subrange_impl<tuple<_Tp...>> = true;

template<class _T1, class _T2>
inline constexpr bool __tuple_like_no_subrange_impl<pair<_T1, _T2>> = true;

template<class _Tp, size_t _Size>
inline constexpr bool __tuple_like_no_subrange_impl<array<_Tp, _Size>> = true;


template<class _Tp>
concept __tuple_like_no_subrange = __tuple_like_no_subrange_impl<remove_cvref_t<_Tp>>;

// This is equivalent to the exposition-only type trait `pair-like`, except that it is false for specializations of
// `ranges::subrange`. This is more useful than the pair-like concept in the standard because every use of `pair-like`
// excludes `ranges::subrange`.
template<class _Tp>
concept __pair_like_no_subrange = __tuple_like_no_subrange<_Tp> && tuple_size<remove_cvref_t<_Tp>>::value == 2;

}// namespace kstd
