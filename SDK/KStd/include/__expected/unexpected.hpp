
#pragma once

#include <__config.hpp>
#include <__expected/unexpect.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_constructible.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/is_swappable.hpp>
#include <__type_traits/remove_cvref.hpp>
#include <__utility/forward.hpp>
#include <__utility/in_place.hpp>
#include <__utility/move.hpp>
#include <__utility/swap.hpp>

namespace kstd {

template <class _Err>
class unexpected {
  static_assert(!is_same_v<remove_cvref_t<_Err>, in_place_t>);
  static_assert(!is_same_v<remove_cvref_t<_Err>, unexpect_t>);

public:
  constexpr unexpected(const unexpected&) = default;
  constexpr unexpected(unexpected&&) = default;
  constexpr unexpected& operator=(const unexpected&) = default;
  constexpr unexpected& operator=(unexpected&&) = default;

  template <class _Up = _Err, enable_if_t<
                                  !is_same_v<remove_cvref_t<_Up>, unexpected> &&
                                      !is_same_v<remove_cvref_t<_Up>, in_place_t> &&
                                      is_constructible_v<_Err, _Up>,
                                  int> = 0>
  constexpr explicit unexpected(_Up&& __u) : __val(forward<_Up>(__u)) {}

  template <class... _Args, enable_if_t<is_constructible_v<_Err, _Args...>, int> = 0>
  constexpr explicit unexpected(in_place_t, _Args&&... __args) : __val(forward<_Args>(__args)...) {}

  constexpr const _Err& error() const & noexcept { return __val; }
  constexpr _Err& error() & noexcept { return __val; }
  constexpr const _Err&& error() const && noexcept { return move(__val); }
  constexpr _Err&& error() && noexcept { return move(__val); }

  constexpr void swap(unexpected& __other) noexcept(__is_nothrow_swappable_v<_Err>) {
    using kstd::swap;
    swap(__val, __other.__val);
  }

  friend constexpr void swap(unexpected& __x, unexpected& __y) noexcept(noexcept(__x.swap(__y))) {
    __x.swap(__y);
  }

  template <class _E2>
  friend constexpr bool operator==(const unexpected& __x, const unexpected<_E2>& __y) {
    return __x.error() == __y.error();
  }

private:
  _Err __val;
};

template <class _Err>
unexpected(_Err) -> unexpected<_Err>;

} // namespace kstd
