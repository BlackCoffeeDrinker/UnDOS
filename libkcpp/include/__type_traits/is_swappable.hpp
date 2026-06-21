
#pragma once

#include <__config.hpp>

#include <__type_traits/is_constructible.hpp>
#include <__type_traits/is_nothrow_assignable.hpp>
#include <__type_traits/void_t.hpp>
#include <__utility/declval.hpp>

namespace kstd {
template<class _Tp, class _Up, class = void>
inline const bool __is_swappable_with_v = false;

template<class _Tp>
inline const bool __is_swappable_v = __is_swappable_with_v<_Tp &, _Tp &>;

template<class _Tp, class _Up, bool = __is_swappable_with_v<_Tp, _Up>>
inline const bool __is_nothrow_swappable_with_v = false;

template<class _Tp>
inline const bool __is_nothrow_swappable_v = __is_nothrow_swappable_with_v<_Tp &, _Tp &>;

template<class _Tp>
using __swap_result_t =
    enable_if_t<is_move_constructible<_Tp>::value && is_move_assignable<_Tp>::value>;

template<class _Tp>
inline constexpr __swap_result_t<_Tp> swap(_Tp &__x, _Tp &__y) noexcept(is_nothrow_move_constructible<_Tp>::value && is_nothrow_move_assignable<_Tp>::value);

template<class _Tp, size_t _Np, enable_if_t<__is_swappable_v<_Tp>, int> = 0>
inline constexpr void swap(_Tp (&__a)[_Np], _Tp (&__b)[_Np]) noexcept(__is_nothrow_swappable_v<_Tp>);

// ALL generic swap overloads MUST already have a declaration available at this point.

template<class _Tp, class _Up>
inline const bool __is_swappable_with_v<_Tp,
                                        _Up,
                                        __void_t<decltype(swap(kstd::declval<_Tp>(), kstd::declval<_Up>())),
                                                 decltype(swap(kstd::declval<_Up>(), kstd::declval<_Tp>()))>> = true;

template<class _Tp, class _Up>
inline const bool __is_nothrow_swappable_with_v<_Tp, _Up, true> =
    noexcept(swap(kstd::declval<_Tp>(), kstd::declval<_Up>())) &&
    noexcept(swap(kstd::declval<_Up>(), kstd::declval<_Tp>()));


template<class _Tp, class _Up>
inline constexpr bool is_swappable_with_v = __is_swappable_with_v<_Tp, _Up>;

template<class _Tp, class _Up>
struct is_swappable_with : bool_constant<is_swappable_with_v<_Tp, _Up>> {};

template<class _Tp>
inline constexpr bool is_swappable_v =
    is_swappable_with_v<__add_lvalue_reference_t<_Tp>, __add_lvalue_reference_t<_Tp>>;

template<class _Tp>
struct is_swappable : bool_constant<is_swappable_v<_Tp>> {};

template<class _Tp, class _Up>
inline constexpr bool is_nothrow_swappable_with_v = __is_nothrow_swappable_with_v<_Tp, _Up>;

template<class _Tp, class _Up>
struct is_nothrow_swappable_with : bool_constant<is_nothrow_swappable_with_v<_Tp, _Up>> {};

template<class _Tp>
inline constexpr bool is_nothrow_swappable_v =
    is_nothrow_swappable_with_v<__add_lvalue_reference_t<_Tp>, __add_lvalue_reference_t<_Tp>>;

template<class _Tp>
struct is_nothrow_swappable : bool_constant<is_nothrow_swappable_v<_Tp>> {};


}// namespace kstd
