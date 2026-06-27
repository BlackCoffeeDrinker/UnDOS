
#pragma once

#include <__config.hpp>

#include <__fwd/memory.hpp>
#include <__memory/construct_at.hpp>
#include <__memory/pointer_traits.hpp>
#include <__type_traits/detected_or.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_constructible.hpp>
#include <__type_traits/is_empty.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/make_unsigned.hpp>
#include <__type_traits/remove_reference.hpp>
#include <__type_traits/void_t.hpp>
#include <__utility/declval.hpp>
#include <__utility/forward.hpp>

namespace kstd {
// __pointer
template<class _Tp>
using __pointer_member = typename _Tp::pointer;

template<class _Tp, class _Alloc>
using __pointer = __detected_or_t<_Tp *, __pointer_member, remove_reference_t<_Alloc>>;

// This trait returns _Alias<_Alloc> if that's well-formed, and _Ptr rebound to _Tp otherwise
template<class _Alloc, template<class> class _Alias, class _Ptr, class _Tp, class = void>
struct __rebind_or_alias_pointer {
  using type = typename pointer_traits<_Ptr>::template rebind<_Tp>;
};

template<class _Ptr, class _Alloc, class _Tp, template<class> class _Alias>
struct __rebind_or_alias_pointer<_Alloc, _Alias, _Ptr, _Tp, __void_t<_Alias<_Alloc>>> {
  using type = _Alias<_Alloc>;
};

// __const_pointer
template<class _Alloc>
using __const_pointer_member = typename _Alloc::const_pointer;

template<class _Tp, class _Ptr, class _Alloc>
using __const_pointer_t =
    typename __rebind_or_alias_pointer<_Alloc, __const_pointer_member, _Ptr, const _Tp>::type;

// __void_pointer
template<class _Alloc>
using __void_pointer_member = typename _Alloc::void_pointer;

template<class _Ptr, class _Alloc>
using __void_pointer_t =
    typename __rebind_or_alias_pointer<_Alloc, __void_pointer_member, _Ptr, void>::type;

// __const_void_pointer
template<class _Alloc>
using __const_void_pointer_member = typename _Alloc::const_void_pointer;

template<class _Ptr, class _Alloc>
using __const_void_pointer_t =
    typename __rebind_or_alias_pointer<_Alloc, __const_void_pointer_member, _Ptr, const void>::type;

// __size_type
template<class _Tp>
using __size_type_member = typename _Tp::size_type;

template<class _Alloc, class _DiffType>
using __size_type = __detected_or_t<make_unsigned_t<_DiffType>, __size_type_member, _Alloc>;

// __alloc_traits_difference_type
template<class _Alloc, class _Ptr, class = void>
struct __alloc_traits_difference_type {
  using type = typename pointer_traits<_Ptr>::difference_type;
};

template<class _Alloc, class _Ptr>
struct __alloc_traits_difference_type<_Alloc, _Ptr, __void_t<typename _Alloc::difference_type>> {
  using type = typename _Alloc::difference_type;
};

// __propagate_on_container_copy_assignment
template<class _Tp>
using __propagate_on_container_copy_assignment_member =
    typename _Tp::propagate_on_container_copy_assignment;

template<class _Alloc>
using __propagate_on_container_copy_assignment =
    __detected_or_t<false_type, __propagate_on_container_copy_assignment_member, _Alloc>;

// __propagate_on_container_move_assignment
template<class _Tp>
using __propagate_on_container_move_assignment_member =
    typename _Tp::propagate_on_container_move_assignment;

template<class _Alloc>
using __propagate_on_container_move_assignment =
    __detected_or_t<false_type, __propagate_on_container_move_assignment_member, _Alloc>;

// __propagate_on_container_swap
template<class _Tp>
using __propagate_on_container_swap_member = typename _Tp::propagate_on_container_swap;

template<class _Alloc>
using __propagate_on_container_swap =
    __detected_or_t<false_type, __propagate_on_container_swap_member, _Alloc>;

// __is_always_equal
template<class _Tp>
using __is_always_equal_member = typename _Tp::is_always_equal;

template<class _Alloc>
using __is_always_equal =
    __detected_or_t<typename is_empty<_Alloc>::type, __is_always_equal_member, _Alloc>;

// __allocator_traits_rebind
template<class _Tp, class _Up, class = void>
inline const bool __has_rebind_other_v = false;
template<class _Tp, class _Up>
inline const bool __has_rebind_other_v<_Tp, _Up, __void_t<typename _Tp::template rebind<_Up>::other>> = true;

template<class _Tp, class _Up, bool = __has_rebind_other_v<_Tp, _Up>>
struct __allocator_traits_rebind {
  static_assert(__has_rebind_other_v<_Tp, _Up>, "This allocator has to implement rebind");
  using type = typename _Tp::template rebind<_Up>::other;
};
template<template<class, class...> class _Alloc, class _Tp, class... _Args, class _Up>
struct __allocator_traits_rebind<_Alloc<_Tp, _Args...>, _Up, true> {
  using type = typename _Alloc<_Tp, _Args...>::template rebind<_Up>::other;
};
template<template<class, class...> class _Alloc, class _Tp, class... _Args, class _Up>
struct __allocator_traits_rebind<_Alloc<_Tp, _Args...>, _Up, false> {
  using type = _Alloc<_Up, _Args...>;
};

template<class _Alloc, class _Tp>
using __allocator_traits_rebind_t = typename __allocator_traits_rebind<_Alloc, _Tp>::type;


// __has_allocate_hint_v
template<class _Alloc, class _SizeType, class _ConstVoidPtr, class = void>
inline const bool __has_allocate_hint_v = false;

template<class _Alloc, class _SizeType, class _ConstVoidPtr>
inline const bool __has_allocate_hint_v<
    _Alloc,
    _SizeType,
    _ConstVoidPtr,
    decltype((void) kstd::declval<_Alloc>().allocate(kstd::declval<_SizeType>(), kstd::declval<_ConstVoidPtr>()))> = true;

// __has_construct_v
template<class, class _Alloc, class... _Args>
inline const bool __has_construct_impl = false;

template<class _Alloc, class... _Args>
inline const bool
    __has_construct_impl<decltype((void) kstd::declval<_Alloc>().construct(kstd::declval<_Args>()...)), _Alloc, _Args...> =
        true;

template<class _Alloc, class... _Args>
inline const bool __has_construct_v = __has_construct_impl<void, _Alloc, _Args...>;

// __has_destroy_v
template<class _Alloc, class _Pointer, class = void>
inline const bool __has_destroy_v = false;

template<class _Alloc, class _Pointer>
inline const bool
    __has_destroy_v<_Alloc, _Pointer, decltype((void) kstd::declval<_Alloc>().destroy(kstd::declval<_Pointer>()))> = true;

// __has_max_size_v
template<class _Alloc, class = void>
inline const bool __has_max_size_v = false;

template<class _Alloc>
inline const bool __has_max_size_v<_Alloc, decltype((void) kstd::declval<_Alloc &>().max_size())> = true;

// __has_select_on_container_copy_construction_v
template<class _Alloc, class = void>
inline const bool __has_select_on_container_copy_construction_v = false;

template<class _Alloc>
inline const bool __has_select_on_container_copy_construction_v<
    _Alloc,
    decltype((void) kstd::declval<_Alloc>().select_on_container_copy_construction())> = true;


template<class _Pointer, class _SizeType = size_t>
struct allocation_result {
  _Pointer ptr;
  _SizeType count;
};
_CTAD_SUPPORTED_FOR_TYPE(allocation_result);


template<class _Alloc>
struct allocator_traits {
  using allocator_type = _Alloc;
  using value_type = typename allocator_type::value_type;
  using pointer = __pointer<value_type, allocator_type>;
  using const_pointer = __const_pointer_t<value_type, pointer, allocator_type>;
  using void_pointer = __void_pointer_t<pointer, allocator_type>;
  using const_void_pointer = __const_void_pointer_t<pointer, allocator_type>;
  using difference_type = typename __alloc_traits_difference_type<allocator_type, pointer>::type;
  using size_type = __size_type<allocator_type, difference_type>;
  using propagate_on_container_copy_assignment = __propagate_on_container_copy_assignment<allocator_type>;
  using propagate_on_container_move_assignment = __propagate_on_container_move_assignment<allocator_type>;
  using propagate_on_container_swap = __propagate_on_container_swap<allocator_type>;
  using is_always_equal = __is_always_equal<allocator_type>;

  template<class _Tp>
  using rebind_alloc = __allocator_traits_rebind_t<allocator_type, _Tp>;
  template<class _Tp>
  using rebind_traits = allocator_traits<rebind_alloc<_Tp>>;

  [[__nodiscard__]] _KSTD_API constexpr static pointer
  allocate(allocator_type &__a, size_type __n) {
    return __a.allocate(__n);
  }

  template<class _Ap = _Alloc, enable_if_t<__has_allocate_hint_v<_Ap, size_type, const_void_pointer>, int> = 0>
  [[__nodiscard__]] _KSTD_API constexpr static pointer
  allocate(allocator_type &__a, size_type __n, const_void_pointer __hint) {
    return __a.allocate(__n, __hint);
  }
  template<class _Ap = _Alloc, enable_if_t<!__has_allocate_hint_v<_Ap, size_type, const_void_pointer>, int> = 0>
  [[__nodiscard__]] _KSTD_API constexpr static pointer
  allocate(allocator_type &__a, size_type __n, const_void_pointer) {
    return __a.allocate(__n);
  }

  template<class _Ap = _Alloc>
  [[nodiscard]] _KSTD_API static constexpr allocation_result<pointer, size_type>
  allocate_at_least(_Ap &__alloc, size_type __n) {
    if constexpr (requires { __alloc.allocate_at_least(__n); }) {
      return __alloc.allocate_at_least(__n);
    } else {
      return {__alloc.allocate(__n), __n};
    }
  }

  _KSTD_API constexpr static void
  deallocate(allocator_type &__a, pointer __p, size_type __n) noexcept {
    __a.deallocate(__p, __n);
  }

  template<class _Tp, class... _Args, enable_if_t<__has_construct_v<allocator_type, _Tp *, _Args...>, int> = 0>
  _KSTD_API constexpr static void
  construct(allocator_type &__a, _Tp *__p, _Args &&...__args) {
    __a.construct(__p, kstd::forward<_Args>(__args)...);
  }
  template<class _Tp, class... _Args, enable_if_t<!__has_construct_v<allocator_type, _Tp *, _Args...>, int> = 0>
  _KSTD_API constexpr static void
  construct(allocator_type &, _Tp *__p, _Args &&...__args) {
    kstd::__construct_at(__p, kstd::forward<_Args>(__args)...);
  }

  template<class _Tp, enable_if_t<__has_destroy_v<allocator_type, _Tp *>, int> = 0>
  _KSTD_API constexpr static void destroy(allocator_type &__a, _Tp *__p) {
    __a.destroy(__p);
  }
  template<class _Tp, enable_if_t<!__has_destroy_v<allocator_type, _Tp *>, int> = 0>
  _KSTD_API constexpr static void destroy(allocator_type &, _Tp *__p) {
    kstd::__destroy_at(__p);
  }

  template<class _Ap = _Alloc, enable_if_t<__has_max_size_v<const _Ap>, int> = 0>
  [[__nodiscard__]] _KSTD_API constexpr static size_type
  max_size(const allocator_type &__a) noexcept {
    return __a.max_size();
  }
  template<class _Ap = _Alloc, enable_if_t<!__has_max_size_v<const _Ap>, int> = 0>
  [[__nodiscard__]] _KSTD_API constexpr static size_type
  max_size(const allocator_type &) noexcept {
    return numeric_limits<size_type>::max() / sizeof(value_type);
  }

  template<class _Ap = _Alloc, enable_if_t<__has_select_on_container_copy_construction_v<const _Ap>, int> = 0>
  [[__nodiscard__]] _KSTD_API constexpr static allocator_type
  select_on_container_copy_construction(const allocator_type &__a) {
    return __a.select_on_container_copy_construction();
  }
  template<class _Ap = _Alloc, enable_if_t<!__has_select_on_container_copy_construction_v<const _Ap>, int> = 0>
  [[__nodiscard__]] _KSTD_API constexpr static allocator_type
  select_on_container_copy_construction(const allocator_type &__a) {
    return __a;
  }
};

#ifndef _LIBCPP_CXX03_LANG
template<class _Traits, class _Tp>
using __rebind_alloc = typename _Traits::template rebind_alloc<_Tp>;
#else
template<class _Traits, class _Tp>
using __rebind_alloc = typename _Traits::template rebind_alloc<_Tp>::other;
#endif

template<class _Alloc>
struct __check_valid_allocator : true_type {
  using _Traits = kstd::allocator_traits<_Alloc>;
  static_assert(is_same<_Alloc, __rebind_alloc<_Traits, typename _Traits::value_type>>::value,
                "[allocator.requirements] states that rebinding an allocator to the same type should result in the "
                "original allocator");
};

// __is_default_allocator_v
template<class _Tp>
inline const bool __is_std_allocator_v = false;

template<class _Tp>
inline const bool __is_std_allocator_v<allocator<_Tp>> = true;

// __is_cpp17_move_insertable_v
template<class _Alloc>
inline const bool __is_cpp17_move_insertable_v =
    is_move_constructible<typename _Alloc::value_type>::value ||
    (!__is_std_allocator_v<_Alloc> &&
     __has_construct_v<_Alloc, typename _Alloc::value_type *, typename _Alloc::value_type &&>);

// __is_cpp17_copy_insertable_v
template<class _Alloc>
inline const bool __is_cpp17_copy_insertable_v =
    __is_cpp17_move_insertable_v<_Alloc> &&
    (is_copy_constructible<typename _Alloc::value_type>::value ||
     (!__is_std_allocator_v<_Alloc> &&
      __has_construct_v<_Alloc, typename _Alloc::value_type *, const typename _Alloc::value_type &>) );

}// namespace kstd
