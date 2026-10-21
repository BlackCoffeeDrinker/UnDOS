
#pragma once

#include <__config.hpp>
#include <__fwd/subrange.hpp>
#include <__tuple/tuple_like_no_subrange.hpp>
#include <__tuple/tuple_size.hpp>
#include <__type_traits/remove_cvref.hpp>

namespace kstd {
template<class _Tp>
inline constexpr bool __is_ranges_subrange_v = false;

template<class _Iter, class _Sent, ranges::subrange_kind _Kind>
inline constexpr bool __is_ranges_subrange_v<ranges::subrange<_Iter, _Sent, _Kind>> = true;

template<class _Tp>
concept __tuple_like = __tuple_like_no_subrange<_Tp> || __is_ranges_subrange_v<remove_cvref_t<_Tp>>;

}// namespace kstd
