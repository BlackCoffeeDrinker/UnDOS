
#pragma once

#include <stddef.hpp>

#include <__config.hpp>

#include <__compare/compare_three_way.hpp>
#include <__compare/compare_three_way_result.hpp>
#include <__compare/three_way_comparable.hpp>
#include <__functional/hash.hpp>
#include <__functional/operations.hpp>
#include <__memory/allocator_traits.hpp>// __pointer
#include <__memory/array_cookie.hpp>
#include <__memory/compressed_pair.hpp>
#include <__memory/pointer_traits.hpp>
#include <__type_traits/add_reference.hpp>
#include <__type_traits/common_type.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/extent.hpp>
#include <__type_traits/dependent_type.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_array.hpp>
#include <__type_traits/is_assignable.hpp>
#include <__type_traits/is_constant_evaluated.hpp>
#include <__type_traits/is_constructible.hpp>
#include <__type_traits/is_convertible.hpp>
#include <__type_traits/is_function.hpp>
#include <__type_traits/is_pointer.hpp>
#include <__type_traits/is_reference.hpp>
#include <__type_traits/is_replaceable.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/is_swappable.hpp>
#include <__type_traits/is_trivially_relocatable.hpp>
#include <__type_traits/is_void.hpp>
#include <__type_traits/remove_extent.hpp>
#include <__type_traits/type_identity.hpp>
#include <__utility/declval.hpp>
#include <__utility/forward.hpp>
#include <__utility/move.hpp>
#include <__utility/private_constructor_tag.hpp>
#include <stdint.h>


namespace kstd {
template<class _Tp>
struct default_delete {
  static_assert(!is_function<_Tp>::value, "default_delete cannot be instantiated for function types");

  _KSTD_API constexpr default_delete() noexcept = default;

  template<class _Up, enable_if_t<is_convertible<_Up *, _Tp *>::value, int> = 0>
  _KSTD_API constexpr default_delete(const default_delete<_Up> &) noexcept {}

  _KSTD_API constexpr void operator()(_Tp *__ptr) const noexcept {
    static_assert(sizeof(_Tp) >= 0, "cannot delete an incomplete type");
    static_assert(!is_void<_Tp>::value, "cannot delete an incomplete type");
    delete __ptr;
  }
};

template<class _Tp>
struct default_delete<_Tp[]> {
  _KSTD_API constexpr default_delete() noexcept = default;

  template<class _Up, enable_if_t<is_convertible<_Up (*)[], _Tp (*)[]>::value, int> = 0>
  _KSTD_API constexpr default_delete(const default_delete<_Up[]> &) noexcept {}

  template<class _Up, enable_if_t<is_convertible<_Up (*)[], _Tp (*)[]>::value, int> = 0>
  _KSTD_API constexpr void operator()(_Up *__ptr) const noexcept {
    static_assert(sizeof(_Up) >= 0, "cannot delete an incomplete type");
    delete[] __ptr;
  }
};


template <class _Tp, class _Dp>
struct __unique_ptr_pointer {
    using type = __detected_or_t<_Tp*, __pointer_member, remove_reference_t<_Dp>>;
};

template <class _Tp, class _Dp = default_delete<_Tp>>
class unique_ptr {
public:
  using element_type = _Tp;
  using deleter_type = _Dp;
  using pointer = typename __unique_ptr_pointer<_Tp, _Dp>::type;

private:
  __compressed_pair<pointer, deleter_type> __ptr_;

public:
  template <class _Dp2 = _Dp>
  constexpr unique_ptr() noexcept requires(is_default_constructible_v<_Dp2> && !is_pointer_v<_Dp2>)
      : __ptr_(pointer(), _Dp2()) {}

  template <class _Dp2 = _Dp>
  constexpr unique_ptr(nullptr_t) noexcept requires(is_default_constructible_v<_Dp2> && !is_pointer_v<_Dp2>)
      : __ptr_(pointer(), _Dp2()) {}

  template <class _Dp2 = _Dp>
  constexpr explicit unique_ptr(pointer __p) noexcept requires(is_default_constructible_v<_Dp2> && !is_pointer_v<_Dp2>)
      : __ptr_(__p, _Dp2()) {}

  template <class _Dp2 = _Dp>
  constexpr unique_ptr(pointer __p, const _Dp2& __d) noexcept requires(is_constructible_v<_Dp2, const _Dp2&>)
      : __ptr_(__p, __d) {}

  template <class _Dp2 = _Dp>
  constexpr unique_ptr(pointer __p, remove_reference_t<_Dp2>&& __d) noexcept requires(is_constructible_v<_Dp2, remove_reference_t<_Dp2>&&>)
      : __ptr_(__p, kstd::move(__d)) {}

  template <class _Dp2 = _Dp>
  constexpr unique_ptr(unique_ptr&& __u) noexcept requires(is_constructible_v<_Dp2, _Dp2>)
      : __ptr_(__u.release(), kstd::forward<_Dp2>(__u.get_deleter())) {}

  template <class _Up, class _Ep>
  constexpr unique_ptr(unique_ptr<_Up, _Ep>&& __u) noexcept
      requires(is_convertible_v<typename unique_ptr<_Up, _Ep>::pointer, pointer> &&
               !is_array_v<_Up> &&
               (is_reference_v<_Dp> ? is_same_v<_Dp, _Ep> : is_convertible_v<_Ep, _Dp>))
      : __ptr_(__u.release(), kstd::forward<_Ep>(__u.get_deleter())) {}

  _KSTD_API constexpr ~unique_ptr() {
    if (__ptr_.first()) {
      __ptr_.second()(__ptr_.first());
    }
  }

  template <class _Dp2 = _Dp>
  constexpr unique_ptr& operator=(unique_ptr&& __u) noexcept requires(is_assignable_v<_Dp2&, _Dp2>) {
    reset(__u.release());
    __ptr_.second() = kstd::forward<_Dp2>(__u.get_deleter());
    return *this;
  }

  template <class _Up, class _Ep>
  constexpr unique_ptr& operator=(unique_ptr<_Up, _Ep>&& __u) noexcept
      requires(is_convertible_v<typename unique_ptr<_Up, _Ep>::pointer, pointer> &&
               !is_array_v<_Up> &&
               is_assignable_v<_Dp&, _Ep&&>) {
    reset(__u.release());
    __ptr_.second() = kstd::forward<_Ep>(__u.get_deleter());
    return *this;
  }

  constexpr unique_ptr& operator=(nullptr_t) noexcept {
    reset();
    return *this;
  }

  constexpr add_lvalue_reference_t<_Tp> operator*() const noexcept {
    return *__ptr_.first();
  }

  constexpr pointer operator->() const noexcept {
    return __ptr_.first();
  }

  constexpr pointer get() const noexcept {
    return __ptr_.first();
  }

  constexpr deleter_type& get_deleter() noexcept {
    return __ptr_.second();
  }

  constexpr const deleter_type& get_deleter() const noexcept {
    return __ptr_.second();
  }

  constexpr explicit operator bool() const noexcept {
    return __ptr_.first() != nullptr;
  }

  constexpr pointer release() noexcept {
    pointer __p = __ptr_.first();
    __ptr_.first() = pointer();
    return __p;
  }

  constexpr void reset(pointer __p = pointer()) noexcept {
    pointer __old = __ptr_.first();
    __ptr_.first() = __p;
    if (__old) {
      __ptr_.second()(__old);
    }
  }

  constexpr void swap(unique_ptr& __u) noexcept {
    pointer __p = __ptr_.first();
    __ptr_.first() = __u.__ptr_.first();
    __u.__ptr_.first() = __p;

    auto __d = kstd::move(__ptr_.second());
    __ptr_.second() = kstd::move(__u.__ptr_.second());
    __u.__ptr_.second() = kstd::move(__d);
  }
};

template <class _Tp, class _Dp>
class unique_ptr<_Tp[], _Dp> {
public:
  using element_type = _Tp;
  using deleter_type = _Dp;
  using pointer = typename __unique_ptr_pointer<_Tp, _Dp>::type;

private:
  __compressed_pair<pointer, deleter_type> __ptr_;

public:
  template <class _Dp2 = _Dp>
  constexpr unique_ptr() noexcept requires(is_default_constructible_v<_Dp2> && !is_pointer_v<_Dp2>)
      : __ptr_(pointer(), _Dp2()) {}

  template <class _Dp2 = _Dp>
  constexpr unique_ptr(nullptr_t) noexcept requires(is_default_constructible_v<_Dp2> && !is_pointer_v<_Dp2>)
      : __ptr_(pointer(), _Dp2()) {}

  template <class _Dp2 = _Dp>
  constexpr explicit unique_ptr(pointer __p) noexcept requires(is_default_constructible_v<_Dp2> && !is_pointer_v<_Dp2>)
      : __ptr_(__p, _Dp2()) {}

  template <class _Dp2 = _Dp>
  constexpr unique_ptr(pointer __p, const _Dp2& __d) noexcept requires(is_constructible_v<_Dp2, const _Dp2&>)
      : __ptr_(__p, __d) {}

  template <class _Dp2 = _Dp>
  constexpr unique_ptr(pointer __p, remove_reference_t<_Dp2>&& __d) noexcept requires(is_constructible_v<_Dp2, remove_reference_t<_Dp2>&&>)
      : __ptr_(__p, kstd::move(__d)) {}

  template <class _Dp2 = _Dp>
  constexpr unique_ptr(unique_ptr&& __u) noexcept requires(is_constructible_v<_Dp2, _Dp2>)
      : __ptr_(__u.release(), kstd::forward<_Dp2>(__u.get_deleter())) {}

  template <class _Up, class _Ep>
  constexpr unique_ptr(unique_ptr<_Up, _Ep>&& __u) noexcept
      requires(is_array_v<_Up> &&
               is_same_v<pointer, typename unique_ptr<_Up, _Ep>::pointer> &&
               is_same_v<element_type, typename unique_ptr<_Up, _Ep>::element_type> &&
               (is_reference_v<_Dp> ? is_same_v<_Dp, _Ep> : is_convertible_v<_Ep, _Dp>))
      : __ptr_(__u.release(), kstd::forward<_Ep>(__u.get_deleter())) {}

  _KSTD_API constexpr ~unique_ptr() {
    if (__ptr_.first()) {
      __ptr_.second()(__ptr_.first());
    }
  }

  template <class _Dp2 = _Dp>
  constexpr unique_ptr& operator=(unique_ptr&& __u) noexcept requires(is_assignable_v<_Dp2&, _Dp2>) {
    reset(__u.release());
    __ptr_.second() = kstd::forward<_Dp2>(__u.get_deleter());
    return *this;
  }

  template <class _Up, class _Ep>
  constexpr unique_ptr& operator=(unique_ptr<_Up, _Ep>&& __u) noexcept
      requires(is_array_v<_Up> &&
               is_same_v<pointer, typename unique_ptr<_Up, _Ep>::pointer> &&
               is_same_v<element_type, typename unique_ptr<_Up, _Ep>::element_type> &&
               is_assignable_v<_Dp&, _Ep&&>) {
    reset(__u.release());
    __ptr_.second() = kstd::forward<_Ep>(__u.get_deleter());
    return *this;
  }

  constexpr unique_ptr& operator=(nullptr_t) noexcept {
    reset();
    return *this;
  }

  constexpr add_lvalue_reference_t<_Tp> operator[](size_t __i) const {
    return __ptr_.first()[__i];
  }

  constexpr pointer get() const noexcept {
    return __ptr_.first();
  }

  constexpr deleter_type& get_deleter() noexcept {
    return __ptr_.second();
  }

  constexpr const deleter_type& get_deleter() const noexcept {
    return __ptr_.second();
  }

  constexpr explicit operator bool() const noexcept {
    return __ptr_.first() != nullptr;
  }

  constexpr pointer release() noexcept {
    pointer __p = __ptr_.first();
    __ptr_.first() = pointer();
    return __p;
  }

  constexpr void reset(pointer __p = pointer()) noexcept {
    pointer __old = __ptr_.first();
    __ptr_.first() = __p;
    if (__old) {
      __ptr_.second()(__old);
    }
  }

  template <class _Up>
  constexpr void reset(_Up) = delete;

  constexpr void swap(unique_ptr& __u) noexcept {
    pointer __p = __ptr_.first();
    __ptr_.first() = __u.__ptr_.first();
    __u.__ptr_.first() = __p;

    auto __d = kstd::move(__ptr_.second());
    __ptr_.second() = kstd::move(__u.__ptr_.second());
    __u.__ptr_.second() = kstd::move(__d);
  }
};

template <class _T1, class _D1, class _T2, class _D2>
constexpr bool operator==(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return __x.get() == __y.get();
}

template <class _T1, class _D1, class _T2, class _D2>
constexpr bool operator!=(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return __x.get() != __y.get();
}

template <class _T1, class _D1>
constexpr bool operator==(const unique_ptr<_T1, _D1>& __x, nullptr_t) noexcept {
  return !__x;
}

template <class _T1, class _D1>
constexpr bool operator!=(const unique_ptr<_T1, _D1>& __x, nullptr_t) noexcept {
  return static_cast<bool>(__x);
}

template <class _T1, class _D1>
constexpr bool operator==(nullptr_t, const unique_ptr<_T1, _D1>& __x) noexcept {
  return !__x;
}

template <class _T1, class _D1>
constexpr bool operator!=(nullptr_t, const unique_ptr<_T1, _D1>& __x) noexcept {
  return static_cast<bool>(__x);
}

template<class _T1, class _D1, class _T2, class _D2>
requires requires(const typename unique_ptr<_T1, _D1>::pointer& __p1, const typename unique_ptr<_T2, _D2>::pointer& __p2) { __p1 <=> __p2; }
constexpr auto operator<=>(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
    return __x.get() <=> __y.get();
}

template<class _T1, class _D1>
requires requires(const typename unique_ptr<_T1, _D1>::pointer& __p1) { __p1 <=> __p1; }
constexpr auto operator<=>(const unique_ptr<_T1, _D1>& __x, nullptr_t) {
    return __x.get() <=> static_cast<typename unique_ptr<_T1, _D1>::pointer>(nullptr);
}

template <class _Tp, class... _Args>
constexpr unique_ptr<_Tp> make_unique(_Args&&... __args) requires(!is_array_v<_Tp>) {
  return unique_ptr<_Tp>(new _Tp(kstd::forward<_Args>(__args)...));
}

template <class _Tp>
constexpr unique_ptr<_Tp> make_unique(size_t __n) requires(is_array_v<_Tp> && extent_v<_Tp> == 0) {
  return unique_ptr<_Tp>(new remove_extent_t<_Tp>[__n]());
}

template <class _Tp, class... _Args>
void make_unique(_Args&&...) requires(is_array_v<_Tp> && extent_v<_Tp> != 0) = delete;

template <class _Tp>
constexpr unique_ptr<_Tp> make_unique_for_overwrite() requires(!is_array_v<_Tp>) {
  return unique_ptr<_Tp>(new _Tp);
}

template <class _Tp>
constexpr unique_ptr<_Tp> make_unique_for_overwrite(size_t __n) requires(is_array_v<_Tp> && extent_v<_Tp> == 0) {
  return unique_ptr<_Tp>(new remove_extent_t<_Tp>[__n]);
}

template <class _Tp, class... _Args>
void make_unique_for_overwrite(_Args&&...) requires(is_array_v<_Tp> && extent_v<_Tp> != 0) = delete;


}// namespace kstd
