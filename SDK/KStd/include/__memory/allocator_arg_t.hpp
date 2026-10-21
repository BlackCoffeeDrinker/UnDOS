
#pragma once

#include <__config.hpp>
#include <__memory/uses_allocator.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_constructible.hpp>
#include <__type_traits/remove_cvref.hpp>
#include <__utility/forward.hpp>

namespace kstd {

struct allocator_arg_t {
  explicit allocator_arg_t() = default;
};

inline constexpr allocator_arg_t allocator_arg = allocator_arg_t();

// allocator construction

template<class _Tp, class _Alloc, class... _Args>
struct __uses_alloc_ctor_imp {
  using _RawAlloc = __remove_cvref_t<_Alloc>;
  static const bool __ua = uses_allocator<_Tp, _RawAlloc>::value;
  static const bool __ic = is_constructible<_Tp, allocator_arg_t, _Alloc, _Args...>::value;
  static const int value = __ua ? 2 - __ic : 0;
};

template<class _Tp, class _Alloc, class... _Args>
struct __uses_alloc_ctor : integral_constant<int, __uses_alloc_ctor_imp<_Tp, _Alloc, _Args...>::value> {};

template<class _Tp, class _Allocator, class... _Args>
inline void
__user_alloc_construct_impl(integral_constant<int, 0>, _Tp *__storage, const _Allocator &, _Args &&...__args) {
  new (__storage) _Tp(kstd::forward<_Args>(__args)...);
}

// FIXME: This should have a version which takes a non-const alloc.
template<class _Tp, class _Allocator, class... _Args>
inline void
__user_alloc_construct_impl(integral_constant<int, 1>, _Tp *__storage, const _Allocator &__a, _Args &&...__args) {
  new (__storage) _Tp(allocator_arg, __a, kstd::forward<_Args>(__args)...);
}

// FIXME: This should have a version which takes a non-const alloc.
template<class _Tp, class _Allocator, class... _Args>
inline void
__user_alloc_construct_impl(integral_constant<int, 2>, _Tp *__storage, const _Allocator &__a, _Args &&...__args) {
  new (__storage) _Tp(kstd::forward<_Args>(__args)..., __a);
}

}// namespace kstd
