
#pragma once

#include <__config.hpp>
#include <__expected/unexpect.hpp>
#include <__expected/unexpected.hpp>
#include <__memory/addressof.hpp>
#include <__memory/construct_at.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_assignable.hpp>
#include <__type_traits/is_constructible.hpp>
#include <__type_traits/is_destructible.hpp>
#include <__type_traits/is_nothrow_constructible.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/is_swappable.hpp>
#include <__type_traits/is_void.hpp>
#include <__type_traits/remove_cvref.hpp>
#include <__utility/forward.hpp>
#include <__utility/in_place.hpp>
#include <__utility/move.hpp>
#include <__utility/swap.hpp>
#include <__fwd/panic.hpp>

namespace kstd {

template <class _Tp, class _Err>
class expected {
  static_assert(!is_void_v<_Tp>, "expected<_Tp, _Err> specialization for void is separate");

  struct __val_tag {};
  struct __err_tag {};

  union _Storage {
    _Tp __val;
    unexpected<_Err> __unexpect;

    constexpr _Storage() {}
    template <class... _Args>
    constexpr _Storage(__val_tag, _Args&&... __args) : __val(forward<_Args>(__args)...) {}
    template <class... _Args>
    constexpr _Storage(__err_tag, _Args&&... __args) : __unexpect(forward<_Args>(__args)...) {}
    ~_Storage() {}
  };

  _Storage __storage;
  bool __has_val;

public:
  using value_type = _Tp;
  using error_type = _Err;
  using unexpected_type = unexpected<_Err>;

  constexpr expected() : __storage(__val_tag{}), __has_val(true) {}

  constexpr expected(const expected& __other) : __has_val(__other.__has_val) {
    if (__has_val) {
      kstd::construct_at(addressof(__storage.__val), __other.__storage.__val);
    } else {
      kstd::construct_at(addressof(__storage.__unexpect), __other.__storage.__unexpect);
    }
  }

  constexpr expected(expected&& __other) noexcept(is_nothrow_move_constructible_v<_Tp> && is_nothrow_move_constructible_v<_Err>)
      : __has_val(__other.__has_val) {
    if (__has_val) {
      kstd::construct_at(addressof(__storage.__val), move(__other.__storage.__val));
    } else {
      kstd::construct_at(addressof(__storage.__unexpect), move(__other.__storage.__unexpect));
    }
  }

  template <class _Up = _Tp, enable_if_t<is_constructible_v<_Tp, _Up> && !is_same_v<remove_cvref_t<_Up>, in_place_t> && !is_same_v<remove_cvref_t<_Up>, expected> && !is_same_v<remove_cvref_t<_Up>, unexpected_type>, int> = 0>
  constexpr expected(_Up&& __v) : __storage(__val_tag{}, forward<_Up>(__v)), __has_val(true) {}

  template <class _G>
  constexpr expected(const unexpected<_G>& __e) : __storage(__err_tag{}, __e), __has_val(false) {}

  template <class _G>
  constexpr expected(unexpected<_G>&& __e) : __storage(__err_tag{}, move(__e)), __has_val(false) {}

  template <class... _Args, enable_if_t<is_constructible_v<_Tp, _Args...>, int> = 0>
  constexpr explicit expected(in_place_t, _Args&&... __args)
      : __storage(__val_tag{}, forward<_Args>(__args)...), __has_val(true) {}

  template <class... _Args, enable_if_t<is_constructible_v<_Err, _Args...>, int> = 0>
  constexpr explicit expected(unexpect_t, _Args&&... __args)
      : __storage(__err_tag{}, in_place, forward<_Args>(__args)...), __has_val(false) {}

  ~expected() {
    if (__has_val) {
      kstd::destroy_at(addressof(__storage.__val));
    } else {
      kstd::destroy_at(addressof(__storage.__unexpect));
    }
  }

  expected& operator=(const expected& __other) {
    if (__has_val && __other.__has_val) {
      __storage.__val = __other.__storage.__val;
    } else if (!__has_val && !__other.__has_val) {
      __storage.__unexpect = __other.__storage.__unexpect;
    } else if (__has_val) {
      kstd::destroy_at(addressof(__storage.__val));
      kstd::construct_at(addressof(__storage.__unexpect), __other.__storage.__unexpect);
      __has_val = false;
    } else {
      kstd::destroy_at(addressof(__storage.__unexpect));
      kstd::construct_at(addressof(__storage.__val), __other.__storage.__val);
      __has_val = true;
    }
    return *this;
  }

  expected& operator=(expected&& __other) noexcept(is_nothrow_move_constructible_v<_Tp> && is_nothrow_move_assignable_v<_Tp> && is_nothrow_move_constructible_v<_Err> && is_nothrow_move_assignable_v<_Err>) {
    if (__has_val && __other.__has_val) {
      __storage.__val = move(__other.__storage.__val);
    } else if (!__has_val && !__other.__has_val) {
      __storage.__unexpect = move(__other.__storage.__unexpect);
    } else if (__has_val) {
      kstd::destroy_at(addressof(__storage.__val));
      kstd::construct_at(addressof(__storage.__unexpect), move(__other.__storage.__unexpect));
      __has_val = false;
    } else {
      kstd::destroy_at(addressof(__storage.__unexpect));
      kstd::construct_at(addressof(__storage.__val), move(__other.__storage.__val));
      __has_val = true;
    }
    return *this;
  }

  constexpr const _Tp* operator->() const noexcept { return addressof(__storage.__val); }
  constexpr _Tp* operator->() noexcept { return addressof(__storage.__val); }
  constexpr const _Tp& operator*() const& noexcept { return __storage.__val; }
  constexpr _Tp& operator*() & noexcept { return __storage.__val; }
  constexpr const _Tp&& operator*() const&& noexcept { return move(__storage.__val); }
  constexpr _Tp&& operator*() && noexcept { return move(__storage.__val); }

  constexpr bool has_value() const noexcept { return __has_val; }
  constexpr explicit operator bool() const noexcept { return __has_val; }

  constexpr const _Tp& value() const& {
    if (!__has_val) panic("bad expected access");
    return __storage.__val;
  }
  constexpr _Tp& value() & {
    if (!__has_val) panic("bad expected access");
    return __storage.__val;
  }

  constexpr const _Err& error() const& {
    if (__has_val) panic("bad expected access (has value)");
    return __storage.__unexpect.error();
  }
  constexpr _Err& error() & {
    if (__has_val) panic("bad expected access (has value)");
    return __storage.__unexpect.error();
  }

  template <class _Up>
  constexpr _Tp value_or(_Up&& __v) const& {
    return __has_val ? __storage.__val : static_cast<_Tp>(forward<_Up>(__v));
  }

  template <class... _Args>
  _Tp& emplace(_Args&&... __args) {
    if (__has_val) {
      kstd::destroy_at(addressof(__storage.__val));
    } else {
      kstd::destroy_at(addressof(__storage.__unexpect));
      __has_val = true;
    }
    kstd::construct_at(addressof(__storage.__val), forward<_Args>(__args)...);
    return __storage.__val;
  }

  void swap(expected& __other) noexcept(__is_nothrow_swappable_v<_Tp> && __is_nothrow_swappable_v<_Err>) {
    if (__has_val && __other.__has_val) {
      using kstd::swap;
      swap(__storage.__val, __other.__storage.__val);
    } else if (!__has_val && !__other.__has_val) {
      __storage.__unexpect.swap(__other.__storage.__unexpect);
    } else if (__has_val) {
      unexpected<_Err> __tmp(move(__other.__storage.__unexpect));
      kstd::destroy_at(addressof(__other.__storage.__unexpect));
      kstd::construct_at(addressof(__other.__storage.__val), move(__storage.__val));
      kstd::destroy_at(addressof(__storage.__val));
      kstd::construct_at(addressof(__storage.__unexpect), move(__tmp));
      __has_val = false;
      __other.__has_val = true;
    } else {
      __other.swap(*this);
    }
  }

  friend void swap(expected& __x, expected& __y) noexcept(noexcept(__x.swap(__y))) {
    __x.swap(__y);
  }

  template <class _T2, class _E2>
  friend constexpr bool operator==(const expected& __x, const expected<_T2, _E2>& __y) {
    if (__x.has_value() != __y.has_value())
      return false;
    if (__x.has_value())
      return *__x == *__y;
    return __x.error() == __y.error();
  }

  template <class _T2>
  friend constexpr bool operator==(const expected& __x, const _T2& __v) {
    return __x.has_value() && *__x == __v;
  }

  template <class _E2>
  friend constexpr bool operator==(const expected& __x, const unexpected<_E2>& __e) {
    return !__x.has_value() && __x.error() == __e.error();
  }
};
template <class _Err>
class expected<void, _Err> {
  struct __err_tag {};
  union _Storage {
    unexpected<_Err> __unexpect;

    constexpr _Storage() {}
    template <class... _Args>
    constexpr _Storage(__err_tag, _Args&&... __args) : __unexpect(forward<_Args>(__args)...) {}
    ~_Storage() {}
  };

  _Storage __storage;
  bool __has_val;

public:
  using value_type = void;
  using error_type = _Err;
  using unexpected_type = unexpected<_Err>;

  constexpr expected() noexcept : __has_val(true) {}

  constexpr expected(const expected& __other) : __has_val(__other.__has_val) {
    if (!__has_val) {
      kstd::construct_at(addressof(__storage.__unexpect), __other.__storage.__unexpect);
    }
  }

  constexpr expected(expected&& __other) noexcept(is_nothrow_move_constructible_v<_Err>)
      : __has_val(__other.__has_val) {
    if (!__has_val) {
      kstd::construct_at(addressof(__storage.__unexpect), move(__other.__storage.__unexpect));
    }
  }

  template <class _G>
  constexpr expected(const unexpected<_G>& __e) : __has_val(false) {
    kstd::construct_at(addressof(__storage.__unexpect), __e);
  }

  template <class _G>
  constexpr expected(unexpected<_G>&& __e) : __has_val(false) {
    kstd::construct_at(addressof(__storage.__unexpect), move(__e));
  }

  template <class... _Args, enable_if_t<is_constructible_v<_Err, _Args...>, int> = 0>
  constexpr explicit expected(unexpect_t, _Args&&... __args)
      : __storage(__err_tag{}, in_place, forward<_Args>(__args)...), __has_val(false) {}

  ~expected() {
    if (!__has_val) {
      kstd::destroy_at(addressof(__storage.__unexpect));
    }
  }

  expected& operator=(const expected& __other) {
    if (__has_val && __other.__has_val) {
      // nothing to do
    } else if (!__has_val && !__other.__has_val) {
      __storage.__unexpect = __other.__storage.__unexpect;
    } else if (__has_val) {
      kstd::construct_at(addressof(__storage.__unexpect), __other.__storage.__unexpect);
      __has_val = false;
    } else {
      kstd::destroy_at(addressof(__storage.__unexpect));
      __has_val = true;
    }
    return *this;
  }

  expected& operator=(expected&& __other) noexcept(is_nothrow_move_constructible_v<_Err> && is_nothrow_move_assignable_v<_Err>) {
    if (__has_val && __other.__has_val) {
      // nothing to do
    } else if (!__has_val && !__other.__has_val) {
      __storage.__unexpect = move(__other.__storage.__unexpect);
    } else if (__has_val) {
      kstd::construct_at(addressof(__storage.__unexpect), move(__other.__storage.__unexpect));
      __has_val = false;
    } else {
      kstd::destroy_at(addressof(__storage.__unexpect));
      __has_val = true;
    }
    return *this;
  }

  constexpr void operator*() const noexcept {}
  constexpr void value() const {
    if (!__has_val) panic("bad expected access");
  }

  constexpr bool has_value() const noexcept { return __has_val; }
  constexpr explicit operator bool() const noexcept { return __has_val; }

  constexpr const _Err& error() const& {
    if (__has_val) panic("bad expected access (has value)");
    return __storage.__unexpect.error();
  }
  constexpr _Err& error() & {
    if (__has_val) panic("bad expected access (has value)");
    return __storage.__unexpect.error();
  }

  void emplace() {
    if (!__has_val) {
      kstd::destroy_at(addressof(__storage.__unexpect));
      __has_val = true;
    }
  }

  void swap(expected& __other) noexcept(__is_nothrow_swappable_v<_Err>) {
    if (__has_val && __other.__has_val) {
      // nothing to do
    } else if (!__has_val && !__other.__has_val) {
      __storage.__unexpect.swap(__other.__storage.__unexpect);
    } else if (__has_val) {
      kstd::construct_at(addressof(__storage.__unexpect), move(__other.__storage.__unexpect));
      kstd::destroy_at(addressof(__other.__storage.__unexpect));
      __has_val = false;
      __other.__has_val = true;
    } else {
      __other.swap(*this);
    }
  }

  friend void swap(expected& __x, expected& __y) noexcept(noexcept(__x.swap(__y))) {
    __x.swap(__y);
  }

  template <class _E2>
  friend constexpr bool operator==(const expected& __x, const expected<void, _E2>& __y) {
    if (__x.has_value() != __y.has_value())
      return false;
    if (__x.has_value())
      return true;
    return __x.error() == __y.error();
  }

  template <class _E2>
  friend constexpr bool operator==(const expected& __x, const unexpected<_E2>& __e) {
    return !__x.has_value() && __x.error() == __e.error();
  }
};

} // namespace kstd
