
#pragma once

#include <__config.hpp>
#include <__memory/addressof.hpp>
#include <__memory/compressed_pair.hpp>
#include <__memory/default_delete.hpp>
#include <__memory/pointer_traits.hpp>
#include <__type_traits/add_reference.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_array.hpp>
#include <__type_traits/is_assignable.hpp>
#include <__type_traits/is_convertible.hpp>
#include <__type_traits/is_nothrow_constructible.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/remove_extent.hpp>
#include <__type_traits/remove_reference.hpp>
#include <__utility/forward.hpp>
#include <__utility/move.hpp>
#include <__type_traits/is_reference.hpp>
#include <__type_traits/void_t.hpp>
#include <compare.hpp>

namespace kstd {

template <class _Tp, class _Dp, class = void>
struct __unique_ptr_pointer {
  using type = _Tp*;
};

template <class _Tp, class _Dp>
struct __unique_ptr_pointer<_Tp, _Dp, void_t<typename _Dp::pointer>> {
  using type = typename _Dp::pointer;
};

template <class _Tp, class _Dp>
using __unique_ptr_pointer_t = typename __unique_ptr_pointer<_Tp, _Dp>::type;

template <class _Tp, class _Dp = default_delete<_Tp>>
class unique_ptr {
public:
  using element_type = _Tp;
  using deleter_type = _Dp;
  using pointer      = __unique_ptr_pointer_t<element_type, deleter_type>;

private:
  __compressed_pair<pointer, deleter_type> __ptr_;

public:
  // Constructors
  constexpr unique_ptr() noexcept : __ptr_(nullptr, deleter_type()) {}
  constexpr unique_ptr(decltype(nullptr)) noexcept : __ptr_(nullptr, deleter_type()) {}
  constexpr explicit unique_ptr(pointer __p) noexcept : __ptr_(__p, deleter_type()) {}

  constexpr unique_ptr(pointer __p, const deleter_type& __d) noexcept : __ptr_(__p, __d) {}
  constexpr unique_ptr(pointer __p, remove_reference_t<deleter_type>&& __d) noexcept
      : __ptr_(__p, kstd::move(__d)) {}

  // Move constructors
  constexpr unique_ptr(unique_ptr&& __u) noexcept
      : __ptr_(__u.release(), kstd::forward<deleter_type>(__u.get_deleter())) {}

  template <class _Up, class _Ep,
            enable_if_t<is_convertible<typename unique_ptr<_Up, _Ep>::pointer, pointer>::value &&
                        !is_array<_Up>::value &&
                        ((is_reference<_Dp>::value && is_same<_Ep, _Dp>::value) ||
                         (!is_reference<_Dp>::value && is_convertible<_Ep, _Dp>::value)), int> = 0>
  constexpr unique_ptr(unique_ptr<_Up, _Ep>&& __u) noexcept
      : __ptr_(__u.release(), kstd::forward<_Ep>(__u.get_deleter())) {}

  // Destructor
  constexpr ~unique_ptr() {
    pointer __p = get();
    if (__p != nullptr)
      get_deleter()(__p);
  }

  // Assignment
  constexpr unique_ptr& operator=(unique_ptr&& __u) noexcept {
    reset(__u.release());
    get_deleter() = kstd::forward<deleter_type>(__u.get_deleter());
    return *this;
  }

  template <class _Up, class _Ep>
  constexpr unique_ptr& operator=(unique_ptr<_Up, _Ep>&& __u) noexcept {
    reset(__u.release());
    get_deleter() = kstd::forward<_Ep>(__u.get_deleter());
    return *this;
  }

  constexpr unique_ptr& operator=(decltype(nullptr)) noexcept {
    reset();
    return *this;
  }

  // Observers
  constexpr add_lvalue_reference_t<_Tp> operator*() const { return *get(); }
  constexpr pointer operator->() const noexcept { return get(); }
  constexpr pointer get() const noexcept { return __ptr_.first(); }
  constexpr deleter_type& get_deleter() noexcept { return __ptr_.second(); }
  constexpr const deleter_type& get_deleter() const noexcept { return __ptr_.second(); }
  constexpr explicit operator bool() const noexcept { return get() != nullptr; }

  // Modifiers
  constexpr pointer release() noexcept {
    pointer __t = get();
    __ptr_.first() = nullptr;
    return __t;
  }

  constexpr void reset(pointer __p = pointer()) noexcept {
    pointer __old = get();
    __ptr_.first() = __p;
    if (__old != nullptr)
      get_deleter()(__old);
  }

  constexpr void swap(unique_ptr& __u) noexcept {
    kstd::swap(__ptr_.first(), __u.__ptr_.first());
    kstd::swap(__ptr_.second(), __u.__ptr_.second());
  }

  // Disable copy
  unique_ptr(const unique_ptr&) = delete;
  unique_ptr& operator=(const unique_ptr&) = delete;
};

// Array specialization
template <class _Tp, class _Dp>
class unique_ptr<_Tp[], _Dp> {
public:
  using element_type = _Tp;
  using deleter_type = _Dp;
  using pointer      = __unique_ptr_pointer_t<element_type, deleter_type>;

private:
  __compressed_pair<pointer, deleter_type> __ptr_;

public:
  // Constructors
  constexpr unique_ptr() noexcept : __ptr_(nullptr, deleter_type()) {}
  constexpr unique_ptr(decltype(nullptr)) noexcept : __ptr_(nullptr, deleter_type()) {}
  constexpr explicit unique_ptr(pointer __p) noexcept : __ptr_(__p, deleter_type()) {}

  template <class _Up,
            enable_if_t<is_convertible<_Up*, pointer>::value &&
                        (is_same<_Dp, default_delete<_Tp[]>>::value ||
                         is_convertible<_Up (*)[], _Tp (*)[]>::value), int> = 0>
  constexpr explicit unique_ptr(_Up* __p) noexcept : __ptr_(__p, deleter_type()) {}

  constexpr unique_ptr(pointer __p, const deleter_type& __d) noexcept : __ptr_(__p, __d) {}
  constexpr unique_ptr(pointer __p, remove_reference_t<deleter_type>&& __d) noexcept
      : __ptr_(__p, kstd::move(__d)) {}

  // Move
  constexpr unique_ptr(unique_ptr&& __u) noexcept
      : __ptr_(__u.release(), kstd::forward<deleter_type>(__u.get_deleter())) {}

  template <class _Up, class _Ep,
            enable_if_t<is_array<_Up>::value &&
                        is_convertible<typename unique_ptr<_Up, _Ep>::pointer, pointer>::value &&
                        is_convertible<typename unique_ptr<_Up, _Ep>::element_type(*)[], element_type(*)[]>::value &&
                        ((is_reference<_Dp>::value && is_same<_Ep, _Dp>::value) ||
                         (!is_reference<_Dp>::value && is_convertible<_Ep, _Dp>::value)), int> = 0>
  constexpr unique_ptr(unique_ptr<_Up, _Ep>&& __u) noexcept
      : __ptr_(__u.release(), kstd::forward<_Ep>(__u.get_deleter())) {}

  // Destructor
  constexpr ~unique_ptr() {
    pointer __p = get();
    if (__p != nullptr)
      get_deleter()(__p);
  }

  // Assignment
  constexpr unique_ptr& operator=(unique_ptr&& __u) noexcept {
    reset(__u.release());
    get_deleter() = kstd::forward<deleter_type>(__u.get_deleter());
    return *this;
  }

  template <class _Up, class _Ep>
  constexpr unique_ptr& operator=(unique_ptr<_Up, _Ep>&& __u) noexcept {
    reset(__u.release());
    get_deleter() = kstd::forward<_Ep>(__u.get_deleter());
    return *this;
  }

  constexpr unique_ptr& operator=(decltype(nullptr)) noexcept {
    reset();
    return *this;
  }

  // Observers
  constexpr _Tp& operator[](size_t __i) const { return get()[__i]; }
  constexpr pointer get() const noexcept { return __ptr_.first(); }
  constexpr deleter_type& get_deleter() noexcept { return __ptr_.second(); }
  constexpr const deleter_type& get_deleter() const noexcept { return __ptr_.second(); }
  constexpr explicit operator bool() const noexcept { return get() != nullptr; }

  // Modifiers
  constexpr pointer release() noexcept {
    pointer __t = get();
    __ptr_.first() = nullptr;
    return __t;
  }

  constexpr void reset(pointer __p = pointer()) noexcept {
    pointer __old = get();
    __ptr_.first() = __p;
    if (__old != nullptr)
      get_deleter()(__old);
  }

  constexpr void reset(decltype(nullptr)) noexcept { reset(); }

  constexpr void swap(unique_ptr& __u) noexcept {
    kstd::swap(__ptr_.first(), __u.__ptr_.first());
    kstd::swap(__ptr_.second(), __u.__ptr_.second());
  }

  // Disable copy
  unique_ptr(const unique_ptr&) = delete;
  unique_ptr& operator=(const unique_ptr&) = delete;
};

// Comparisons
template <class _T1, class _D1, class _T2, class _D2>
constexpr bool operator==(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return __x.get() == __y.get();
}

template <class _T1, class _D1, class _T2, class _D2>
constexpr bool operator!=(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return !(__x == __y);
}

template <class _T1, class _D1, class _T2, class _D2>
requires requires { kstd::declval<typename unique_ptr<_T1, _D1>::pointer>() <=> kstd::declval<typename unique_ptr<_T2, _D2>::pointer>(); }
constexpr auto operator<=>(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return __x.get() <=> __y.get();
}

template <class _Tp, class _Dp>
constexpr bool operator==(const unique_ptr<_Tp, _Dp>& __x, decltype(nullptr)) noexcept {
  return !__x;
}

template <class _Tp, class _Dp>
requires requires { kstd::declval<typename unique_ptr<_Tp, _Dp>::pointer>() <=> static_cast<typename unique_ptr<_Tp, _Dp>::pointer>(nullptr); }
constexpr auto operator<=>(const unique_ptr<_Tp, _Dp>& __x, decltype(nullptr)) noexcept {
  return __x.get() <=> static_cast<typename unique_ptr<_Tp, _Dp>::pointer>(nullptr);
}

// make_unique
template <class _Tp, class... _Args>
inline constexpr enable_if_t<!is_array<_Tp>::value, unique_ptr<_Tp>>
make_unique(_Args&&... __args) {
  return unique_ptr<_Tp>(new _Tp(kstd::forward<_Args>(__args)...));
}

template <class _Tp>
inline constexpr enable_if_t<is_unbounded_array<_Tp>::value, unique_ptr<_Tp>>
make_unique(size_t __n) {
  typedef remove_extent_t<_Tp> _Up;
  return unique_ptr<_Tp>(new _Up[__n]());
}

template <class _Tp, class... _Args>
enable_if_t<is_bounded_array<_Tp>::value> make_unique(_Args&&...) = delete;

// make_unique_for_overwrite
template <class _Tp>
inline constexpr enable_if_t<!is_array<_Tp>::value, unique_ptr<_Tp>>
make_unique_for_overwrite() {
  return unique_ptr<_Tp>(new _Tp);
}

template <class _Tp>
inline constexpr enable_if_t<is_unbounded_array<_Tp>::value, unique_ptr<_Tp>>
make_unique_for_overwrite(size_t __n) {
  typedef remove_extent_t<_Tp> _Up;
  return unique_ptr<_Tp>(new _Up[__n]);
}

template <class _Tp, class... _Args>
enable_if_t<is_bounded_array<_Tp>::value> make_unique_for_overwrite(_Args&&...) = delete;

template <class _Tp, class _Dp>
constexpr void swap(unique_ptr<_Tp, _Dp>& __x, unique_ptr<_Tp, _Dp>& __y) noexcept {
  __x.swap(__y);
}

} // namespace kstd
