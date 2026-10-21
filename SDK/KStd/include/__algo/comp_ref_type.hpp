
#pragma once

#include <__config.hpp>

#include <__utility/declval.hpp>

namespace kstd {

template<class _Compare>
struct __debug_less {
  _Compare &__comp_;
  constexpr _KSTD_API __debug_less(_Compare &__c) : __comp_(__c) {}

  template<class _Tp, class _Up>
  constexpr _KSTD_API bool operator()(const _Tp &__x, const _Up &__y) {
    bool __r = __comp_(__x, __y);
    if (__r)
      __do_compare_assert(0, __y, __x);
    return __r;
  }

  template<class _Tp, class _Up>
  constexpr _KSTD_API bool operator()(_Tp &__x, _Up &__y) {
    bool __r = __comp_(__x, __y);
    if (__r)
      __do_compare_assert(0, __y, __x);
    return __r;
  }

  template<class _LHS, class _RHS>
  constexpr inline _KSTD_API decltype((void) kstd::declval<_Compare &>()(kstd::declval<_LHS &>(), kstd::declval<_RHS &>()))
  __do_compare_assert(int, _LHS &__l, _RHS &__r) {
    _LIBCPP_ASSERT_SEMANTIC_REQUIREMENT(!__comp_(__l, __r), "Comparator does not induce a strict weak ordering");
    (void) __l;
    (void) __r;
  }

  template<class _LHS, class _RHS>
  constexpr inline _KSTD_API void __do_compare_assert(long, _LHS &, _RHS &) {}
};

template <class _Comp>
using __comp_ref_type = _Comp&;

}// namespace kstd
