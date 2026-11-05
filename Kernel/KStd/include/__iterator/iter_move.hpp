
#pragma once
#include <__concepts/class_or_enum.hpp>
#include <__config.hpp>
#include <__iterator/iterator_traits.hpp>
#include <__type_traits/is_reference.hpp>
#include <__type_traits/is_referenceable.hpp>
#include <__type_traits/remove_cvref.hpp>
#include <__utility/declval.hpp>
#include <__utility/forward.hpp>
#include <__utility/move.hpp>

namespace kstd {

// [iterator.cust.move]

namespace ranges {
namespace __iter_move {

void iter_move() = delete;

template<class _Tp>
concept __unqualified_iter_move = __class_or_enum<remove_cvref_t<_Tp>> && requires(_Tp &&__t) {
  // NOLINTNEXTLINE(libcpp-robust-against-adl) iter_move ADL calls should only be made through ranges::iter_move
  iter_move(kstd::forward<_Tp>(__t));
};

template<class _Tp>
concept __move_deref = !__unqualified_iter_move<_Tp> && requires(_Tp &&__t) {
  *__t;
  requires is_lvalue_reference_v<decltype(*__t)>;
};

template<class _Tp>
concept __just_deref = !__unqualified_iter_move<_Tp> && !__move_deref<_Tp> && requires(_Tp &&__t) {
  *__t;
  requires(!is_lvalue_reference_v<decltype(*__t)>);
};

// [iterator.cust.move]

struct __fn {
  // NOLINTBEGIN(libcpp-robust-against-adl) iter_move ADL calls should only be made through ranges::iter_move
  template<class _Ip>
    requires __unqualified_iter_move<_Ip>
  [[nodiscard]] constexpr decltype(auto) operator()(_Ip &&__i) const
      noexcept(noexcept(iter_move(kstd::forward<_Ip>(__i)))) {
    return iter_move(kstd::forward<_Ip>(__i));
  }
  // NOLINTEND(libcpp-robust-against-adl)

  template<class _Ip>
    requires __move_deref<_Ip>
  [[nodiscard]] constexpr auto operator()(_Ip &&__i) const
      noexcept(noexcept(kstd::move(*kstd::forward<_Ip>(__i)))) -> decltype(kstd::move(*kstd::forward<_Ip>(__i))) {
    return kstd::move(*kstd::forward<_Ip>(__i));
  }

  template<class _Ip>
    requires __just_deref<_Ip>
  [[nodiscard]] constexpr auto operator()(_Ip &&__i) const
      noexcept(noexcept(*kstd::forward<_Ip>(__i))) -> decltype(*kstd::forward<_Ip>(__i)) {
    return *kstd::forward<_Ip>(__i);
  }
};
}// namespace __iter_move

inline namespace __cpo {
inline constexpr auto iter_move = __iter_move::__fn{};
}// namespace __cpo
}// namespace ranges

template<__dereferenceable _Tp>
  requires requires(_Tp &__t) {
    { ranges::iter_move(__t) } -> __referenceable;
  }
using iter_rvalue_reference_t = decltype(ranges::iter_move(kstd::declval<_Tp &>()));

}// namespace kstd
