
#pragma once

#include <__config.hpp>
#include <__utility/forward.hpp>

namespace kstd {
struct compare_three_way {
  template<class _T1, class _T2>
    requires three_way_comparable_with<_T1, _T2>
  constexpr _KSTD_API auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) <=> kstd::forward<_T2>(__u))) {
    return kstd::forward<_T1>(__t) <=> kstd::forward<_T2>(__u);
  }

  using is_transparent = void;
};
}// namespace kstd
