#pragma once

#include <__concepts/common_reference_with.hpp>
#include <__concepts/same_as.hpp>
#include <__config.hpp>
#include <__type_traits/is_reference.hpp>
#include <__type_traits/make_const_lvalue_ref.hpp>
#include <__utility/forward.hpp>

namespace kstd {

// [concept.assignable]

template<class _Lhs, class _Rhs>
concept assignable_from =
    is_lvalue_reference_v<_Lhs> &&
    common_reference_with<__make_const_lvalue_ref<_Lhs>, __make_const_lvalue_ref<_Rhs>> &&
    requires(_Lhs __lhs, _Rhs &&__rhs) {
      { __lhs = kstd::forward<_Rhs>(__rhs) } -> same_as<_Lhs>;
    };

}// namespace kstd
