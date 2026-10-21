
#pragma once

#include <__concepts/boolean_testable.hpp>
#include <__concepts/common_reference_with.hpp>
#include <__concepts/comparison_common_type.hpp>
#include <__config.hpp>
#include <__type_traits/common_reference.hpp>
#include <__type_traits/make_const_lvalue_ref.hpp>

namespace kstd {

// [concept.equalitycomparable]

template<class _Tp, class _Up>
concept __weakly_equality_comparable_with =
    requires(__make_const_lvalue_ref<_Tp> __t, __make_const_lvalue_ref<_Up> __u) {
      { __t == __u } -> __boolean_testable;
      { __t != __u } -> __boolean_testable;
      { __u == __t } -> __boolean_testable;
      { __u != __t } -> __boolean_testable;
    };

template<class _Tp>
concept equality_comparable = __weakly_equality_comparable_with<_Tp, _Tp>;

// clang-format off
template <class _Tp, class _Up>
concept equality_comparable_with =
    equality_comparable<_Tp> && equality_comparable<_Up> &&
    __comparison_common_type_with<_Tp, _Up> &&
    equality_comparable<
        common_reference_t<
            __make_const_lvalue_ref<_Tp>,
            __make_const_lvalue_ref<_Up>>> &&
    __weakly_equality_comparable_with<_Tp, _Up>;
// clang-format on

}// namespace kstd
