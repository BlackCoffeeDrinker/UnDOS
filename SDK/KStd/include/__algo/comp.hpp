
#pragma once

#include <__config.hpp>

#include <__type_traits/desugars_to.hpp>
#include <__type_traits/is_generic_transparent_comparator.hpp>
#include <__type_traits/is_integral.hpp>


namespace kstd {

struct __equal_to {
  template<class _T1, class _T2>
  _KSTD_API constexpr bool operator()(const _T1 &__x, const _T2 &__y) const {
    return __x == __y;
  }
};

template<class _Tp, class _Up>
inline const bool __desugars_to_v<__equal_tag, __equal_to, _Tp, _Up> = true;

// The definition is required because __less is part of the ABI, but it's empty
// because all comparisons should be transparent.
template<class _T1 = void, class _T2 = _T1>
struct __less {};

template<>
struct __less<void, void> {
  template<class _Tp, class _Up>
  _KSTD_API constexpr bool operator()(const _Tp &__lhs, const _Up &__rhs) const {
    return __lhs < __rhs;
  }
};

template<class _Tp>
inline const bool __desugars_to_v<__less_tag, __less<>, _Tp, _Tp> = true;

template<class _Tp>
inline const bool __desugars_to_v<__totally_ordered_less_tag, __less<>, _Tp, _Tp> = is_integral<_Tp>::value;

template<>
inline const bool __is_generic_transparent_comparator_v<__less<>> = true;

}// namespace kstd
