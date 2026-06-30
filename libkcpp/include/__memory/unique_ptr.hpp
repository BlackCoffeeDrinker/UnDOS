
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
#include <__utility/swap.hpp>

namespace kstd {

template <class _Tp, class _Dp = default_delete<_Tp>>
class unique_ptr {
public:
  typedef _Tp element_type;
  typedef _Dp deleter_type;
  typedef __pointer_of_t<_Dp> pointer;

private:
  __compressed_pair<pointer, deleter_type> __ptr_;

public:
  // Constructors
  constexpr unique_ptr() noexcept : __ptr_(nullptr, deleter_type()) {}
  constexpr unique_ptr(decltype(nullptr)) noexcept : __ptr_(nullptr, deleter_type()) {}
  explicit unique_ptr(pointer __p) noexcept : __ptr_(__p, deleter_type()) {}

  unique_ptr(pointer __p, const deleter_type& __d) noexcept : __ptr_(__p, __d) {}
  unique_ptr(pointer __p, remove_reference_t<deleter_type>&& __d) noexcept
      : __ptr_(__p, kstd::move(__d)) {}

  // Move constructors
  unique_ptr(unique_ptr&& __u) noexcept
      : __ptr_(__u.release(), kstd::forward<deleter_type>(__u.get_deleter())) {}

  template <class _Up, class _Ep,
            enable_if_t<is_convertible<typename unique_ptr<_Up, _Ep>::pointer, pointer>::value &&
                        !is_array<_Up>::value &&
                        ((is_reference<_Dp>::value && is_same<_Ep, _Dp>::value) ||
                         (!is_reference<_Dp>::value && is_convertible<_Ep, _Dp>::value)), int> = 0>
  unique_ptr(unique_ptr<_Up, _Ep>&& __u) noexcept
      : __ptr_(__u.release(), kstd::forward<_Ep>(__u.get_deleter())) {}

  // Destructor
  ~unique_ptr() {
    pointer __p = get();
    if (__p != nullptr)
      get_deleter()(__p);
  }

  // Assignment
  unique_ptr& operator=(unique_ptr&& __u) noexcept {
    reset(__u.release());
    get_deleter() = kstd::forward<deleter_type>(__u.get_deleter());
    return *this;
  }

  template <class _Up, class _Ep>
  unique_ptr& operator=(unique_ptr<_Up, _Ep>&& __u) noexcept {
    reset(__u.release());
    get_deleter() = kstd::forward<_Ep>(__u.get_deleter());
    return *this;
  }

  unique_ptr& operator=(decltype(nullptr)) noexcept {
    reset();
    return *this;
  }

  // Observers
  add_lvalue_reference_t<_Tp> operator*() const { return *get(); }
  pointer operator->() const noexcept { return get(); }
  pointer get() const noexcept { return __ptr_.first(); }
  deleter_type& get_deleter() noexcept { return __ptr_.second(); }
  const deleter_type& get_deleter() const noexcept { return __ptr_.second(); }
  explicit operator bool() const noexcept { return get() != nullptr; }

  // Modifiers
  pointer release() noexcept {
    pointer __t = get();
    __ptr_.first() = nullptr;
    return __t;
  }

  void reset(pointer __p = pointer()) noexcept {
    pointer __old = get();
    __ptr_.first() = __p;
    if (__old != nullptr)
      get_deleter()(__old);
  }

  void swap(unique_ptr& __u) noexcept {
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
  typedef _Tp element_type;
  typedef _Dp deleter_type;
  typedef __pointer_of_t<_Dp> pointer;

private:
  __compressed_pair<pointer, deleter_type> __ptr_;

public:
  // Constructors
  constexpr unique_ptr() noexcept : __ptr_(nullptr, deleter_type()) {}
  constexpr unique_ptr(decltype(nullptr)) noexcept : __ptr_(nullptr, deleter_type()) {}
  explicit unique_ptr(pointer __p) noexcept : __ptr_(__p, deleter_type()) {}

  // Move
  unique_ptr(unique_ptr&& __u) noexcept
      : __ptr_(__u.release(), kstd::forward<deleter_type>(__u.get_deleter())) {}

  // Destructor
  ~unique_ptr() {
    pointer __p = get();
    if (__p != nullptr)
      get_deleter()(__p);
  }

  // Assignment
  unique_ptr& operator=(unique_ptr&& __u) noexcept {
    reset(__u.release());
    get_deleter() = kstd::forward<deleter_type>(__u.get_deleter());
    return *this;
  }

  unique_ptr& operator=(decltype(nullptr)) noexcept {
    reset();
    return *this;
  }

  // Observers
  _Tp& operator[](size_t __i) const { return get()[__i]; }
  pointer get() const noexcept { return __ptr_.first(); }
  deleter_type& get_deleter() noexcept { return __ptr_.second(); }
  const deleter_type& get_deleter() const noexcept { return __ptr_.second(); }
  explicit operator bool() const noexcept { return get() != nullptr; }

  // Modifiers
  pointer release() noexcept {
    pointer __t = get();
    __ptr_.first() = nullptr;
    return __t;
  }

  void reset(pointer __p = pointer()) noexcept {
    pointer __old = get();
    __ptr_.first() = __p;
    if (__old != nullptr)
      get_deleter()(__old);
  }

  void reset(decltype(nullptr)) noexcept { reset(); }

  void swap(unique_ptr& __u) noexcept {
    kstd::swap(__ptr_.first(), __u.__ptr_.first());
    kstd::swap(__ptr_.second(), __u.__ptr_.second());
  }

  // Disable copy
  unique_ptr(const unique_ptr&) = delete;
  unique_ptr& operator=(const unique_ptr&) = delete;
};

template <class _Tp, class... _Args>
inline enable_if_t<!is_array<_Tp>::value, unique_ptr<_Tp>>
make_unique(_Args&&... __args) {
  return unique_ptr<_Tp>(new _Tp(kstd::forward<_Args>(__args)...));
}

template <class _Tp>
inline enable_if_t<is_array<_Tp>::value, unique_ptr<_Tp>>
make_unique(size_t __n) {
  typedef remove_extent_t<_Tp> _Up;
  return unique_ptr<_Tp>(new _Up[__n]());
}

} // namespace kstd
