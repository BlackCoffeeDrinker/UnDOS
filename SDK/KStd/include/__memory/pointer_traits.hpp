
#pragma once

#include <__config.hpp>
#include <__memory/addressof.hpp>
#include <__type_traits/bool_constant.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/conjunction.hpp>
#include <__type_traits/decay.hpp>
#include <__type_traits/detected_or.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_class.hpp>
#include <__type_traits/is_function.hpp>
#include <__type_traits/is_void.hpp>
#include <__type_traits/nat.hpp>
#include <__type_traits/void_t.hpp>
#include <__utility/declval.hpp>
#include <__utility/forward.hpp>

namespace kstd {

template<class _Ptr>
struct __pointer_traits_element_type_impl {};

template<template<class, class...> class _Sp, class _Tp, class... _Args>
struct __pointer_traits_element_type_impl<_Sp<_Tp, _Args...>> {
  using type = _Tp;
};

template<class _Ptr, class = void>
struct __pointer_traits_element_type : __pointer_traits_element_type_impl<_Ptr> {};

template<class _Ptr>
struct __pointer_traits_element_type<_Ptr, __void_t<typename _Ptr::element_type>> {
  using type = typename _Ptr::element_type;
};

template<class _Tp, class _Up>
struct __pointer_traits_rebind_impl {
  static_assert(false, "Cannot rebind pointer; did you forget to add a rebind member to your pointer?");
};

template<template<class, class...> class _Sp, class _Tp, class... _Args, class _Up>
struct __pointer_traits_rebind_impl<_Sp<_Tp, _Args...>, _Up> {
  using type = _Sp<_Up, _Args...>;
};

template<class _Tp, class _Up, class = void>
struct __pointer_traits_rebind : __pointer_traits_rebind_impl<_Tp, _Up> {};

template<class _Tp, class _Up>
struct __pointer_traits_rebind<_Tp, _Up, __void_t<typename _Tp::template rebind<_Up>>> {
#ifndef _LIBCPP_CXX03_LANG
  using type = typename _Tp::template rebind<_Up>;
#else
  using type = typename _Tp::template rebind<_Up>::other;
#endif
};

template<class _Tp>
using __difference_type_member = typename _Tp::difference_type;

template<class _Ptr, class = void>
struct __pointer_traits_impl {};

template<class _Ptr>
struct __pointer_traits_impl<_Ptr, __void_t<typename __pointer_traits_element_type<_Ptr>::type>> {
  typedef _Ptr pointer;
  typedef typename __pointer_traits_element_type<pointer>::type element_type;
  using difference_type = __detected_or_t<ptrdiff_t, __difference_type_member, pointer>;

#ifndef _LIBCPP_CXX03_LANG
  template<class _Up>
  using rebind = typename __pointer_traits_rebind<pointer, _Up>::type;
#else
  template<class _Up>
  struct rebind {
    typedef typename __pointer_traits_rebind<pointer, _Up>::type other;
  };
#endif// _LIBCPP_CXX03_LANG

  public:
  constexpr static pointer
  pointer_to(__conditional_t<is_void<element_type>::value, __nat, element_type> &__r) {
    return pointer::pointer_to(__r);
  }
};

template<class _Ptr>
struct pointer_traits : __pointer_traits_impl<_Ptr> {};

template<class _Tp>
struct pointer_traits<_Tp *> {
  typedef _Tp *pointer;
  typedef _Tp element_type;
  typedef ptrdiff_t difference_type;

#ifndef _LIBCPP_CXX03_LANG
  template<class _Up>
  using rebind = _Up *;
#else
  template<class _Up>
  struct rebind {
    typedef _Up *other;
  };
#endif

  public:
  constexpr static pointer
  pointer_to(__conditional_t<is_void<element_type>::value, __nat, element_type> &__r) noexcept {
    return kstd::addressof(__r);
  }
};

#ifndef _LIBCPP_CXX03_LANG
template<class _From, class _To>
using __rebind_pointer_t = typename pointer_traits<_From>::template rebind<_To>;
#else
template<class _From, class _To>
using __rebind_pointer_t = typename pointer_traits<_From>::template rebind<_To>::other;
#endif

// to_address

template<class _Pointer, class = void>
struct __to_address_helper;

template<class _Tp>
constexpr _Tp *__to_address(_Tp *__p) noexcept {
  static_assert(!is_function<_Tp>::value, "_Tp is a function type");
  return __p;
}

template<class _Pointer, class = void>
struct _HasToAddress : false_type {};

template<class _Pointer>
struct _HasToAddress<_Pointer, decltype((void) pointer_traits<_Pointer>::to_address(kstd::declval<const _Pointer &>()))>
    : true_type {};

template<class _Pointer, class = void>
struct _HasArrow : false_type {};

template<class _Pointer>
struct _HasArrow<_Pointer, decltype((void) kstd::declval<const _Pointer &>().operator->())> : true_type {};

template<class _Pointer>
struct _IsFancyPointer {
  static const bool value = _HasArrow<_Pointer>::value || _HasToAddress<_Pointer>::value;
};

// enable_if is needed here to avoid instantiating checks for fancy pointers on raw pointers
template<class _Pointer, enable_if_t<_And<is_class<_Pointer>, _IsFancyPointer<_Pointer>>::value, int> = 0>

constexpr decay_t<decltype(__to_address_helper<_Pointer>::__call(kstd::declval<const _Pointer &>()))>
__to_address(const _Pointer &__p) noexcept {
  return __to_address_helper<_Pointer>::__call(__p);
}

template<class _Pointer, class>
struct __to_address_helper {

  constexpr static decltype(kstd::__to_address(kstd::declval<const _Pointer &>().operator->()))
  __call(const _Pointer &__p) noexcept {
    return kstd::__to_address(__p.operator->());
  }
};

template<class _Pointer>
struct __to_address_helper<_Pointer,
                           decltype((void) pointer_traits<_Pointer>::to_address(kstd::declval<const _Pointer &>()))> {

  constexpr static decltype(pointer_traits<_Pointer>::to_address(kstd::declval<const _Pointer &>()))
  __call(const _Pointer &__p) noexcept {
    return pointer_traits<_Pointer>::to_address(__p);
  }
};

template<class _Tp>
inline constexpr auto to_address(_Tp *__p) noexcept {
  return kstd::__to_address(__p);
}

template<class _Pointer>
inline constexpr auto to_address(const _Pointer &__p) noexcept
    -> decltype(kstd::__to_address(__p)) {
  return kstd::__to_address(__p);
}


template<class _Tp>
struct __pointer_of {};

template<class _Tp>
concept __has_pointer_member = requires { typename _Tp::pointer; };

template<class _Tp>
concept __has_element_type_member = requires { typename _Tp::element_type; };

template<class _Tp>
  requires __has_pointer_member<_Tp>
struct __pointer_of<_Tp> {
  using type = typename _Tp::pointer;
};

template<class _Tp>
  requires(!__has_pointer_member<_Tp> && __has_element_type_member<_Tp>)
struct __pointer_of<_Tp> {
  using type = typename _Tp::element_type *;
};

template<class _Tp>
  requires(!__has_pointer_member<_Tp> && !__has_element_type_member<_Tp> &&
           __has_element_type_member<pointer_traits<_Tp>>)
struct __pointer_of<_Tp> {
  using type = typename pointer_traits<_Tp>::element_type *;
};

template<typename _Tp>
using __pointer_of_t = typename __pointer_of<_Tp>::type;

template<typename _Tp, typename _Up>
using __pointer_of_or_t = __detected_or_t<_Up, __pointer_of_t, _Tp>;

template<class _Smart>
concept __resettable_smart_pointer = requires(_Smart __s) { __s.reset(); };

template<class _Smart, class _Pointer, class... _Args>
concept __resettable_smart_pointer_with_args = requires(_Smart __s, _Pointer __p, _Args... __args) {
  __s.reset(static_cast<__pointer_of_or_t<_Smart, _Pointer>>(__p), kstd::forward<_Args>(__args)...);
};


// This function ensures safe conversions between fancy pointers at compile-time, where we avoid casts from/to
// `__void_pointer` by obtaining the underlying raw pointer from the fancy pointer using `kstd::to_address`,
// then dereferencing it to retrieve the pointed-to object, and finally constructing the target fancy pointer
// to that object using the `kstd::pointer_traits<>::pointer_to` function.
template<class _PtrTo, class _PtrFrom>
constexpr _PtrTo __static_fancy_pointer_cast(const _PtrFrom &__p) {
  using __ptr_traits = pointer_traits<_PtrTo>;
  using __element_type = typename __ptr_traits::element_type;
  return __p ? __ptr_traits::pointer_to(*static_cast<__element_type *>(kstd::addressof(*__p)))
             : static_cast<_PtrTo>(nullptr);
}
}// namespace kstd
