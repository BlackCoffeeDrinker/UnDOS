#pragma once

#include <__concepts/assignable.hpp>
#include <__concepts/class_or_enum.hpp>
#include <__concepts/common_reference_with.hpp>
#include <__concepts/constructible.hpp>
#include <__config.hpp>
#include <__type_traits/extent.hpp>
#include <__type_traits/is_nothrow_assignable.hpp>
#include <__type_traits/is_nothrow_constructible.hpp>
#include <__type_traits/remove_cvref.hpp>
#include <__utility/exchange.hpp>
#include <__utility/forward.hpp>
#include <__utility/move.hpp>

namespace kstd {


namespace ranges {
namespace __swap {

template<class _Tp>
void swap(_Tp &, _Tp &) = delete;

// clang-format off
template <class _Tp, class _Up>
concept __unqualified_swappable_with =
    (__class_or_enum<remove_cvref_t<_Tp>> || __class_or_enum<remove_cvref_t<_Up>>) &&
    requires(_Tp&& __t, _Up&& __u) {
        swap(kstd::forward<_Tp>(__t), kstd::forward<_Up>(__u));
    };
// clang-format on

struct __fn;

// clang-format off
template <class _Tp, class _Up, size_t _Size>
concept __swappable_arrays =
    !__unqualified_swappable_with<_Tp (&)[_Size], _Up (&)[_Size]> &&
    extent_v<_Tp> == extent_v<_Up> &&
    requires(_Tp (&__t)[_Size], _Up (&__u)[_Size], const __fn& __swap) {
        __swap(__t[0], __u[0]);
    };
// clang-format on

template<class _Tp>
concept __exchangeable =
    !__unqualified_swappable_with<_Tp &, _Tp &> && move_constructible<_Tp> && assignable_from<_Tp &, _Tp>;

struct __fn {
  // 2.1   `S` is `(void)swap(E1, E2)`* if `E1` or `E2` has class or enumeration type and...
  // *The name `swap` is used here unqualified.
  template<class _Tp, class _Up>
    requires __unqualified_swappable_with<_Tp, _Up>
  constexpr void operator()(_Tp &&__t, _Up &&__u) const
      noexcept(noexcept(swap(kstd::forward<_Tp>(__t), kstd::forward<_Up>(__u)))) {
    swap(kstd::forward<_Tp>(__t), kstd::forward<_Up>(__u));
  }

  // 2.2   Otherwise, if `E1` and `E2` are lvalues of array types with equal extent and...
  template<class _Tp, class _Up, size_t _Size>
    requires __swappable_arrays<_Tp, _Up, _Size>
  constexpr void operator()(_Tp (&__t)[_Size], _Up (&__u)[_Size]) const
      noexcept(noexcept((*this)(*__t, *__u))) {
    // TODO(cjdb): replace with `ranges::swap_ranges`.
    for (size_t __i = 0; __i < _Size; ++__i) {
      (*this)(__t[__i], __u[__i]);
    }
  }

  // 2.3   Otherwise, if `E1` and `E2` are lvalues of the same type `T` that models...
  template<__exchangeable _Tp>
  constexpr void operator()(_Tp &__x, _Tp &__y) const
      noexcept(is_nothrow_move_constructible_v<_Tp> && is_nothrow_move_assignable_v<_Tp>) {
    __y = kstd::exchange(__x, kstd::move(__y));
  }
};
}// namespace __swap

inline namespace __cpo {
inline constexpr auto swap = __swap::__fn{};
}// namespace __cpo
}// namespace ranges

template<class _Tp>
concept swappable = requires(_Tp &__a, _Tp &__b) { ranges::swap(__a, __b); };

template<class _Tp, class _Up>
concept swappable_with = common_reference_with<_Tp, _Up> && requires(_Tp &&__t, _Up &&__u) {
  ranges::swap(kstd::forward<_Tp>(__t), kstd::forward<_Tp>(__t));
  ranges::swap(kstd::forward<_Up>(__u), kstd::forward<_Up>(__u));
  ranges::swap(kstd::forward<_Tp>(__t), kstd::forward<_Up>(__u));
  ranges::swap(kstd::forward<_Up>(__u), kstd::forward<_Tp>(__t));
};

}// namespace kstd
