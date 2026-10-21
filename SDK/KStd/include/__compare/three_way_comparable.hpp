
#pragma once

#include <__compare/common_comparison_category.hpp>
#include <__compare/ordering.hpp>
#include <__concepts/equality_comparable.hpp>
#include <__concepts/same_as.hpp>
#include <__concepts/totally_ordered.hpp>
#include <__config.hpp>
#include <__type_traits/remove_reference.hpp>

#include <__utility/declval.hpp>

namespace kstd {

template<class _Tp, class _Up = _Tp>
struct compare_three_way_result {
  using type = decltype(declval<const remove_reference_t<_Tp>&>() <=> declval<const remove_reference_t<_Up>&>());
};

template<class _Tp, class _Up = _Tp>
using compare_three_way_result_t = typename compare_three_way_result<_Tp, _Up>::type;

template<class _Tp, class _Cat>
concept __compares_as = same_as<common_comparison_category_t<_Tp, _Cat>, _Cat>;

template<class _Tp, class _Cat = partial_ordering>
concept three_way_comparable =
    __weakly_equality_comparable_with<_Tp, _Tp> && __partially_ordered_with<_Tp, _Tp> &&
    requires(__make_const_lvalue_ref<_Tp> __a, __make_const_lvalue_ref<_Tp> __b) {
      { __a <=> __b } -> __compares_as<_Cat>;
    };

template<class _Tp, class _Up, class _Cat = partial_ordering>
concept three_way_comparable_with =
    three_way_comparable<_Tp, _Cat> && three_way_comparable<_Up, _Cat> && __comparison_common_type_with<_Tp, _Up> &&
    three_way_comparable<common_reference_t<__make_const_lvalue_ref<_Tp>, __make_const_lvalue_ref<_Up>>, _Cat> &&
    __weakly_equality_comparable_with<_Tp, _Up> && __partially_ordered_with<_Tp, _Up> &&
    requires(__make_const_lvalue_ref<_Tp> __t, __make_const_lvalue_ref<_Up> __u) {
      { __t <=> __u } -> __compares_as<_Cat>;
      { __u <=> __t } -> __compares_as<_Cat>;
    };

}// namespace kstd
