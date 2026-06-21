
#pragma once

#include <__compare/three_way_comparable.hpp>
#include <__concepts/boolean_testable.hpp>
#include <__config.hpp>
#include <__utility/declval.hpp>

namespace kstd {
inline constexpr auto __synth_three_way = []<class _Tp, class _Up>(const _Tp &__t, const _Up &__u)
  requires requires {
    { __t < __u } -> __boolean_testable;
    { __u < __t } -> __boolean_testable;
  }
{
  if constexpr (three_way_comparable_with<_Tp, _Up>) {
    return __t <=> __u;
  } else {
    if (__t < __u)
      return weak_ordering::less;
    if (__u < __t)
      return weak_ordering::greater;
    return weak_ordering::equivalent;
  }
};

template<class _Tp, class _Up = _Tp>
using __synth_three_way_result = decltype(kstd::__synth_three_way(kstd::declval<_Tp &>(), kstd::declval<_Up &>()));

}// namespace kstd
