
#pragma once


#include <__config.hpp>
#include <__type_traits/is_nothrow_assignable.hpp>
#include <__type_traits/is_nothrow_constructible.hpp>
#include <__utility/forward.hpp>
#include <__utility/move.hpp>

namespace kstd {
template<class _T1, class _T2 = _T1>
inline constexpr _T1 exchange(_T1 &__obj, _T2 &&__new_value) noexcept(
    is_nothrow_move_constructible<_T1>::value && is_nothrow_assignable<_T1 &, _T2>::value) {
  _T1 __old_value = kstd::move(__obj);
  __obj = kstd::forward<_T2>(__new_value);
  return __old_value;
}
}// namespace kstd
