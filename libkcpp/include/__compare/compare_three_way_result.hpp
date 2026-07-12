
#pragma once

#include <__config.hpp>
#include <__type_traits/make_const_lvalue_ref.hpp>
#include <__utility/declval.hpp>

namespace kstd {

template<class, class, class>
struct _KSTD_API __compare_three_way_result {};

template<class _Tp, class _Up>
struct _KSTD_API __compare_three_way_result<
    _Tp,
    _Up,
    decltype(kstd::declval<__make_const_lvalue_ref<_Tp>>() <=> kstd::declval<__make_const_lvalue_ref<_Up>>(), void())> {
  using type =
      decltype(kstd::declval<__make_const_lvalue_ref<_Tp>>() <=> kstd::declval<__make_const_lvalue_ref<_Up>>());
};

}// namespace kstd
