
#pragma once
#include <__config.hpp>

#include <__compare/common_comparison_category.hpp>
#include <__compare/ordering.hpp>
#include <__compare/synth_three_way.hpp>

#include <__concepts/boolean_testable.hpp>

#include <__fwd/array.hpp>
#include <__fwd/get.hpp>
#include <__fwd/pair.hpp>
#include <__fwd/tuple.hpp>

#include <__memory/allocator_arg_t.hpp>
#include <__memory/uses_allocator.hpp>

#include <__tuple/find_index.hpp>
#include <__tuple/ignore.hpp>
#include <__tuple/tuple_element.hpp>
#include <__tuple/tuple_like.hpp>
#include <__tuple/tuple_size.hpp>

#include <__type_traits/common_reference.hpp>
#include <__type_traits/common_type.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/conjunction.hpp>
#include <__type_traits/copy_cvref.hpp>
#include <__type_traits/disjunction.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/invoke.hpp>
#include <__type_traits/is_assignable.hpp>
#include <__type_traits/is_constructible.hpp>
#include <__type_traits/is_convertible.hpp>
#include <__type_traits/is_empty.hpp>
#include <__type_traits/is_final.hpp>
#include <__type_traits/is_implicitly_default_constructible.hpp>
#include <__type_traits/is_nothrow_assignable.hpp>
#include <__type_traits/is_nothrow_constructible.hpp>
#include <__type_traits/is_reference.hpp>
#include <__type_traits/is_replaceable.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/is_swappable.hpp>
#include <__type_traits/is_trivially_relocatable.hpp>
#include <__type_traits/lazy.hpp>
#include <__type_traits/maybe_const.hpp>
#include <__type_traits/nat.hpp>
#include <__type_traits/negation.hpp>
#include <__type_traits/reference_constructs_from_temporary.hpp>
#include <__type_traits/remove_cv.hpp>
#include <__type_traits/remove_cvref.hpp>
#include <__type_traits/remove_reference.hpp>
#include <__type_traits/type_list.hpp>
#include <__type_traits/unwrap_ref.hpp>

#include <__utility/declval.hpp>
#include <__utility/forward.hpp>
#include <__utility/index_sequence.hpp>
#include <__utility/integer_sequence.hpp>
#include <__utility/move.hpp>
#include <__utility/swap.hpp>

namespace kstd {

template<size_t _Ip, class _Tp, class _Up>
constexpr bool __tuple_compare_equal(const _Tp &__x, const _Up &__y) {
  if constexpr (_Ip == 0)
    return true;
  else
    return kstd::__tuple_compare_equal<_Ip - 1>(__x, __y) && kstd::get<_Ip - 1>(__x) == kstd::get<_Ip - 1>(__y);
}

template<class _Ret, class _Tp, class _Up, size_t... _Is>
constexpr _Ret __tuple_compare_three_way(const _Tp &__x, const _Up &__y, index_sequence<_Is...>) {
  _Ret __result = strong_ordering::equal;
  static_cast<void>(
      ((__result = kstd::__synth_three_way(kstd::get<_Is>(__x), kstd::get<_Is>(__y)), __result != 0) || ...));
  return __result;
}

template<class _Tp>
concept __tuple_like_no_tuple = __tuple_like<_Tp> && !__is_tuple_v<_Tp>;

template<class _Tp, class _Up, class _IndexSeq>
struct __tuple_common_comparison_category_impl {};

// TODO(LLVM 23): Remove `tuple_size_v<_Tp> == tuple_size_v<_Up>` here once once LLVM-20 support ends
// because the resolution of CWG2369 landed in LLVM-21.
template<class _Tp, class _Up, size_t... _Is>
  requires(tuple_size_v<_Tp> == tuple_size_v<_Up>) && requires {
    typename common_comparison_category_t<
        __synth_three_way_result<tuple_element_t<_Is, _Tp>, tuple_element_t<_Is, _Up>>...>;
  }
struct __tuple_common_comparison_category_impl<_Tp, _Up, index_sequence<_Is...>> {
  using type =
      common_comparison_category_t<__synth_three_way_result<tuple_element_t<_Is, _Tp>, tuple_element_t<_Is, _Up>>...>;
};

template<__tuple_like _Tp, __tuple_like _Up>
using __tuple_common_comparison_category =
    __tuple_common_comparison_category_impl<_Tp, _Up, make_index_sequence<tuple_size_v<_Tp>>>::type;
// __tuple_leaf

template<size_t _Ip, class _Hp, bool = is_empty<_Hp>::value && !is_final_v<_Hp>>
class __tuple_leaf;

template<size_t _Ip, class _Hp, bool _Ep>
inline constexpr void
swap(__tuple_leaf<_Ip, _Hp, _Ep> &__x, __tuple_leaf<_Ip, _Hp, _Ep> &__y) noexcept(is_nothrow_swappable_v<_Hp>) {
  swap(__x.get(), __y.get());
}

template<size_t _Ip, class _Hp, bool _Ep>
constexpr void
swap(const __tuple_leaf<_Ip, _Hp, _Ep> &__x,
     const __tuple_leaf<_Ip, _Hp, _Ep> &__y) noexcept(is_nothrow_swappable_v<const _Hp>) {
  swap(__x.get(), __y.get());
}

template<size_t _Ip, class _Hp, bool>
class __tuple_leaf {
  _Hp __value_;

  public:
  constexpr __tuple_leaf &operator=(const __tuple_leaf &) = delete;

  constexpr __tuple_leaf() noexcept(is_nothrow_default_constructible<_Hp>::value) : __value_() {
    static_assert(!is_reference<_Hp>::value, "Attempted to default construct a reference element in a tuple");
  }

  template<class _Alloc>
  constexpr __tuple_leaf(integral_constant<int, 0>, const _Alloc &) : __value_() {
    static_assert(!is_reference<_Hp>::value, "Attempted to default construct a reference element in a tuple");
  }

  template<class _Alloc>
  constexpr __tuple_leaf(integral_constant<int, 1>, const _Alloc &__a)
      : __value_(allocator_arg_t(), __a) {
    static_assert(!is_reference<_Hp>::value, "Attempted to default construct a reference element in a tuple");
  }

  template<class _Alloc>
  constexpr __tuple_leaf(integral_constant<int, 2>, const _Alloc &__a) : __value_(__a) {
    static_assert(!is_reference<_Hp>::value, "Attempted to default construct a reference element in a tuple");
  }

  template<
      class _Tp,
      enable_if_t<_And<_IsNotSame<__remove_cvref_t<_Tp>, __tuple_leaf>, is_constructible<_Hp, _Tp>>::value, int> = 0>

  constexpr explicit __tuple_leaf(_Tp &&__t) noexcept(is_nothrow_constructible<_Hp, _Tp>::value)
      : __value_(kstd::forward<_Tp>(__t)) {
    static_assert(!reference_constructs_from_temporary_v<_Hp, _Tp &&>,
                  "Attempted construction of reference element binds to a temporary whose lifetime has ended");
  }

  template<class _Tp, class _Alloc>

  constexpr explicit __tuple_leaf(integral_constant<int, 0>, const _Alloc &, _Tp &&__t)
      : __value_(kstd::forward<_Tp>(__t)) {
    static_assert(!reference_constructs_from_temporary_v<_Hp, _Tp &&>,
                  "Attempted construction of reference element binds to a temporary whose lifetime has ended");
  }

  template<class _Tp, class _Alloc>

  constexpr explicit __tuple_leaf(integral_constant<int, 1>, const _Alloc &__a, _Tp &&__t)
      : __value_(allocator_arg_t(), __a, kstd::forward<_Tp>(__t)) {
    static_assert(!is_reference<_Hp>::value, "Attempted to uses-allocator construct a reference element in a tuple");
  }

  template<class _Tp, class _Alloc>

  constexpr explicit __tuple_leaf(integral_constant<int, 2>, const _Alloc &__a, _Tp &&__t)
      : __value_(kstd::forward<_Tp>(__t), __a) {
    static_assert(!is_reference<_Hp>::value, "Attempted to uses-allocator construct a reference element in a tuple");
  }

  __tuple_leaf(const __tuple_leaf &__t) = default;
  __tuple_leaf(__tuple_leaf &&__t) = default;

  constexpr int
  swap(__tuple_leaf &__t) noexcept(is_nothrow_swappable_v<__tuple_leaf>) {
    kstd::swap(*this, __t);
    return 0;
  }

  constexpr int swap(const __tuple_leaf &__t) const
      noexcept(is_nothrow_swappable_v<const __tuple_leaf>) {
    kstd::swap(*this, __t);
    return 0;
  }

  constexpr _Hp &get() noexcept { return __value_; }
  constexpr const _Hp &get() const noexcept { return __value_; }
};

template<size_t _Ip, class _Hp>
class __tuple_leaf<_Ip, _Hp, true> : private remove_cv_t<_Hp> {
  public:
  constexpr __tuple_leaf &operator=(const __tuple_leaf &) = delete;

  constexpr __tuple_leaf() noexcept(is_nothrow_default_constructible<_Hp>::value) {}

  template<class _Alloc>
  constexpr __tuple_leaf(integral_constant<int, 0>, const _Alloc &) {}

  template<class _Alloc>
  constexpr __tuple_leaf(integral_constant<int, 1>, const _Alloc &__a)
      : _Hp(allocator_arg_t(), __a) {}

  template<class _Alloc>
  constexpr __tuple_leaf(integral_constant<int, 2>, const _Alloc &__a) : _Hp(__a) {}

  template<class _Tp,
           enable_if_t<_And<_IsNotSame<__remove_cvref_t<_Tp>, __tuple_leaf>, is_constructible<_Hp, _Tp>>::value,
                       int> = 0>

  constexpr explicit __tuple_leaf(_Tp &&__t) noexcept(is_nothrow_constructible<_Hp, _Tp>::value)
      : _Hp(kstd::forward<_Tp>(__t)) {}

  template<class _Tp, class _Alloc>
  constexpr explicit __tuple_leaf(integral_constant<int, 0>, const _Alloc &, _Tp &&__t)
      : _Hp(kstd::forward<_Tp>(__t)) {}

  template<class _Tp, class _Alloc>
  constexpr explicit __tuple_leaf(integral_constant<int, 1>, const _Alloc &__a, _Tp &&__t)
      : _Hp(allocator_arg_t(), __a, kstd::forward<_Tp>(__t)) {}

  template<class _Tp, class _Alloc>
  constexpr explicit __tuple_leaf(integral_constant<int, 2>, const _Alloc &__a, _Tp &&__t)
      : _Hp(kstd::forward<_Tp>(__t), __a) {}

  __tuple_leaf(__tuple_leaf const &) = default;
  __tuple_leaf(__tuple_leaf &&) = default;

  constexpr int
  swap(__tuple_leaf &__t) noexcept(is_nothrow_swappable_v<__tuple_leaf>) {
    kstd::swap(*this, __t);
    return 0;
  }

  constexpr int swap(const __tuple_leaf &__rhs) const
      noexcept(is_nothrow_swappable_v<const __tuple_leaf>) {
    kstd::swap(*this, __rhs);
    return 0;
  }

  constexpr _Hp &get() noexcept { return static_cast<_Hp &>(*this); }
  constexpr const _Hp &get() const noexcept {
    return static_cast<const _Hp &>(*this);
  }
};

template<class... _Tp>
constexpr void __swallow(_Tp &&...) noexcept {}

// __tuple_impl

template<class _Indx, class... _Tp>
struct __tuple_impl;

struct __forward_args {};
struct __value_init {};
struct __from_tuple {};

template<size_t... _Indx, class... _Tp>
struct
    __tuple_impl<index_sequence<_Indx...>, _Tp...> : public __tuple_leaf<_Indx, _Tp>... {
  constexpr __tuple_impl() noexcept(
      __all<is_nothrow_default_constructible<_Tp>::value...>::value) {}

  template<class... _Args>
  constexpr explicit __tuple_impl(__forward_args, _Args &&...__args)
      : __tuple_leaf<_Indx, _Tp>(kstd::forward<_Args>(__args))... {}

  template<class _Alloc>

  constexpr explicit __tuple_impl(allocator_arg_t, const _Alloc &__alloc, __value_init)
      : __tuple_leaf<_Indx, _Tp>(__uses_alloc_ctor<_Tp, _Alloc>(), __alloc)... {}

  template<class _Alloc, class... _Args>
  constexpr explicit __tuple_impl(
      allocator_arg_t, const _Alloc &__alloc, __forward_args, _Args &&...__args)
      : __tuple_leaf<_Indx, _Tp>(__uses_alloc_ctor<_Tp, _Alloc, _Args>(), __alloc, kstd::forward<_Args>(__args))... {}

  template<class _Tuple>
  constexpr __tuple_impl(__from_tuple, _Tuple &&__t) noexcept(
      (__all<is_nothrow_constructible<_Tp, __copy_cvref_t<_Tuple, tuple_element_t<_Indx, __remove_cvref_t<_Tuple>>>>::
                 value...>::value))
      : __tuple_leaf<_Indx, _Tp>(
            kstd::forward<__copy_cvref_t<_Tuple, tuple_element_t<_Indx, __remove_cvref_t<_Tuple>>>>(
                kstd::get<_Indx>(__t)))... {}

  template<class _Alloc, class _Tuple>
  constexpr __tuple_impl(allocator_arg_t, const _Alloc &__a, __from_tuple, _Tuple &&__t)
      : __tuple_leaf<_Indx, _Tp>(
            __uses_alloc_ctor<_Tp,
                              _Alloc,
                              __copy_cvref_t<_Tuple, tuple_element_t<_Indx, __remove_cvref_t<_Tuple>>>>(),
            __a,
            kstd::forward<__copy_cvref_t<_Tuple, tuple_element_t<_Indx, __remove_cvref_t<_Tuple>>>>(
                kstd::get<_Indx>(__t)))... {}

  __tuple_impl(const __tuple_impl &) = default;
  __tuple_impl(__tuple_impl &&) = default;

  constexpr void
  swap(__tuple_impl &__t) noexcept(__all<is_nothrow_swappable_v<_Tp>...>::value) {
    kstd::__swallow(__tuple_leaf<_Indx, _Tp>::swap(static_cast<__tuple_leaf<_Indx, _Tp> &>(__t))...);
  }

  constexpr void swap(const __tuple_impl &__t) const
      noexcept(__all<is_nothrow_swappable_v<const _Tp>...>::value) {
    kstd::__swallow(__tuple_leaf<_Indx, _Tp>::swap(static_cast<const __tuple_leaf<_Indx, _Tp> &>(__t))...);
  }
};

template<class _Dest, class _Source, size_t... _Np>
constexpr void
__memberwise_copy_assign(_Dest &__dest, _Source const &__source, index_sequence<_Np...>) {
  kstd::__swallow(((kstd::get<_Np>(__dest) = kstd::get<_Np>(__source)), void(), 0)...);
}

template<class _Dest, class _Source, class... _Up, size_t... _Np>
constexpr void
__memberwise_forward_assign(_Dest &__dest, _Source &&__source, __type_list<_Up...>, index_sequence<_Np...>) {
  kstd::__swallow(((kstd::get<_Np>(__dest) = kstd::forward<_Up>(kstd::get<_Np>(__source))), void(), 0)...);
}

template<class... _Tp>
class tuple {
  typedef __tuple_impl<index_sequence_for<_Tp...>, _Tp...> _BaseT;

  _BaseT __base_;

  template<size_t _Jp, class... _Up>
  friend constexpr typename tuple_element<_Jp, tuple<_Up...>>::type &get(tuple<_Up...> &) noexcept;
  template<size_t _Jp, class... _Up>
  friend constexpr const typename tuple_element<_Jp, tuple<_Up...>>::type &
  get(const tuple<_Up...> &) noexcept;
  template<size_t _Jp, class... _Up>
  friend constexpr typename tuple_element<_Jp, tuple<_Up...>>::type &&
  get(tuple<_Up...> &&) noexcept;
  template<size_t _Jp, class... _Up>
  friend constexpr const typename tuple_element<_Jp, tuple<_Up...>>::type &&
  get(const tuple<_Up...> &&) noexcept;

  public:
  using __trivially_relocatable =
      __conditional_t<_And<__internal_is_trivially_relocatable<_Tp>...>::value, tuple, void>;
  using __replaceable = __conditional_t<_And<is_replaceable<_Tp>...>::value, tuple, void>;

  // [tuple.cnstr]

  // tuple() constructors (including allocator_arg_t variants)
  template<template<class...> class _IsImpDefault = is_implicitly_default_constructible,
           template<class...> class _IsDefault = is_default_constructible,
           enable_if_t<_And<_IsDefault<_Tp>...>::value, int> = 0>
  constexpr explicit(_Not<_Lazy<_And, _IsImpDefault<_Tp>...>>::value)
      tuple() noexcept(_And<is_nothrow_default_constructible<_Tp>...>::value) {}

  template<class _Alloc,
           template<class...> class _IsImpDefault = is_implicitly_default_constructible,
           template<class...> class _IsDefault = is_default_constructible,
           enable_if_t<_And<_IsDefault<_Tp>...>::value, int> = 0>
  constexpr explicit(_Not<_Lazy<_And, _IsImpDefault<_Tp>...>>::value)
      tuple(allocator_arg_t, _Alloc const &__a)
      : __base_(allocator_arg_t(), __a, __value_init{}) {}

  // tuple(const T&...) constructors (including allocator_arg_t variants)
  template<template<class...> class _And = _And,
           enable_if_t<_And<bool_constant<sizeof...(_Tp) >= 1>, is_copy_constructible<_Tp>...>::value, int> = 0>

  constexpr explicit(_Not<_Lazy<_And, is_convertible<const _Tp &, _Tp>...>>::value)
      tuple(const _Tp &...__t) noexcept(_And<is_nothrow_copy_constructible<_Tp>...>::value)
      : __base_(__forward_args{}, __t...) {}

  template<class _Alloc,
           template<class...> class _And = _And,
           enable_if_t<_And<bool_constant<sizeof...(_Tp) >= 1>, is_copy_constructible<_Tp>...>::value, int> = 0>

  constexpr explicit(_Not<_Lazy<_And, is_convertible<const _Tp &, _Tp>...>>::value)
      tuple(allocator_arg_t, const _Alloc &__a, const _Tp &...__t)
      : __base_(allocator_arg_t(), __a, __forward_args{}, __t...) {}

  // tuple(U&& ...) constructors (including allocator_arg_t variants)
  template<class... _Up>
  struct _IsThisTuple : false_type {};
  template<class _Up>
  struct _IsThisTuple<_Up> : is_same<__remove_cvref_t<_Up>, tuple> {};

  template<class... _Up>
  struct _EnableUTypesCtor
      : _And<bool_constant<sizeof...(_Tp) >= 1>,
             _Not<_IsThisTuple<_Up...>>,// extension to allow mis-behaved user constructors
             is_constructible<_Tp, _Up>...> {};

  template<class... _Up,
           enable_if_t<_And<bool_constant<sizeof...(_Up) == sizeof...(_Tp)>, _EnableUTypesCtor<_Up...>>::value,
                       int> = 0>
  constexpr explicit(_Not<_Lazy<_And, is_convertible<_Up, _Tp>...>>::value)
      tuple(_Up &&...__u) noexcept(_And<is_nothrow_constructible<_Tp, _Up>...>::value)
      : __base_(__forward_args{}, kstd::forward<_Up>(__u)...) {}

  template<class _Alloc,
           class... _Up,
           enable_if_t<_And<bool_constant<sizeof...(_Up) == sizeof...(_Tp)>, _EnableUTypesCtor<_Up...>>::value,
                       int> = 0>
  constexpr explicit(_Not<_Lazy<_And, is_convertible<_Up, _Tp>...>>::value)
      tuple(allocator_arg_t, const _Alloc &__a, _Up &&...__u)
      : __base_(allocator_arg_t(), __a, __forward_args{}, kstd::forward<_Up>(__u)...) {}

  // Copy and move constructors (including the allocator_arg_t variants)
  tuple(const tuple &) = default;
  tuple(tuple &&) = default;

  template<class _Alloc,
           template<class...> class _And = _And,
           enable_if_t<_And<is_copy_constructible<_Tp>...>::value, int> = 0>
  constexpr tuple(allocator_arg_t, const _Alloc &__alloc, const tuple &__t)
      : __base_(allocator_arg_t(), __alloc, __from_tuple(), __t) {}

  template<class _Alloc,
           template<class...> class _And = _And,
           enable_if_t<_And<is_move_constructible<_Tp>...>::value, int> = 0>
  constexpr tuple(allocator_arg_t, const _Alloc &__alloc, tuple &&__t)
      : __base_(allocator_arg_t(), __alloc, __from_tuple(), kstd::move(__t)) {}

  // tuple(const tuple<U...>&) constructors (including allocator_arg_t variants)

  template<class _OtherTuple, class _DecayedOtherTuple = __remove_cvref_t<_OtherTuple>, class = void>
  struct _EnableCtorFromUTypesTuple : false_type {};

  template<class _OtherTuple, class... _Up>
  struct _EnableCtorFromUTypesTuple<
      _OtherTuple,
      tuple<_Up...>,
      // the length of the packs needs to checked first otherwise the 2 packs cannot be expanded simultaneously below
      enable_if_t<sizeof...(_Up) == sizeof...(_Tp)>>
      : _And<
            // the two conditions below are not in spec. The purpose is to disable the UTypes Ctor when copy/move Ctor
            // can work. Otherwise, is_constructible can trigger hard error in those cases
            // https://godbolt.org/z/M94cGdKcE
            _Not<is_same<_OtherTuple, const tuple &>>,
            _Not<is_same<_OtherTuple, tuple &&>>,
            is_constructible<_Tp, __copy_cvref_t<_OtherTuple, _Up>>...,
            _Lazy<_Or,
                  bool_constant<sizeof...(_Tp) != 1>,
                  // _Tp and _Up are 1-element packs - the pack expansions look
                  // weird to avoid tripping up the type traits in degenerate cases
                  _Lazy<_And,
                        _Not<is_same<_Tp, _Up>>...,
                        _Not<is_convertible<_OtherTuple, _Tp>>...,
                        _Not<is_constructible<_Tp, _OtherTuple>>...>>> {};

  template<class... _Up, enable_if_t<_And<_EnableCtorFromUTypesTuple<const tuple<_Up...> &>>::value, int> = 0>

  constexpr explicit(_Not<_Lazy<_And, is_convertible<const _Up &, _Tp>...>>::value)
      tuple(const tuple<_Up...> &__t) noexcept(_And<is_nothrow_constructible<_Tp, const _Up &>...>::value)
      : __base_(__from_tuple(), __t) {}

  template<class... _Up,
           class _Alloc,
           enable_if_t<_And<_EnableCtorFromUTypesTuple<const tuple<_Up...> &>>::value, int> = 0>

  constexpr explicit(_Not<_Lazy<_And, is_convertible<const _Up &, _Tp>...>>::value)
      tuple(allocator_arg_t, const _Alloc &__a, const tuple<_Up...> &__t)
      : __base_(allocator_arg_t(), __a, __from_tuple(), __t) {}

  // tuple(tuple<U...>&) constructors (including allocator_arg_t variants)

  template<class... _Up, enable_if_t<_EnableCtorFromUTypesTuple<tuple<_Up...> &>::value> * = nullptr>
  constexpr explicit(!_Lazy<_And, is_convertible<_Up &, _Tp>...>::value) tuple(tuple<_Up...> &__t)
      : __base_(__from_tuple(), __t) {}

  template<class _Alloc, class... _Up, enable_if_t<_EnableCtorFromUTypesTuple<tuple<_Up...> &>::value> * = nullptr>
  constexpr explicit(!_Lazy<_And, is_convertible<_Up &, _Tp>...>::value)
      tuple(allocator_arg_t, const _Alloc &__alloc, tuple<_Up...> &__t)
      : __base_(allocator_arg_t(), __alloc, __from_tuple(), __t) {}

  // tuple(tuple<U...>&&) constructors (including allocator_arg_t variants)
  template<class... _Up, enable_if_t<_And<_EnableCtorFromUTypesTuple<tuple<_Up...> &&>>::value, int> = 0>
  constexpr explicit(_Not<_Lazy<_And, is_convertible<_Up, _Tp>...>>::value)
      tuple(tuple<_Up...> &&__t) noexcept(_And<is_nothrow_constructible<_Tp, _Up>...>::value)
      : __base_(__from_tuple(), kstd::move(__t)) {}

  template<class _Alloc,
           class... _Up,
           enable_if_t<_And<_EnableCtorFromUTypesTuple<tuple<_Up...> &&>>::value, int> = 0>
  constexpr explicit(_Not<_Lazy<_And, is_convertible<_Up, _Tp>...>>::value)
      tuple(allocator_arg_t, const _Alloc &__a, tuple<_Up...> &&__t)
      : __base_(allocator_arg_t(), __a, __from_tuple(), kstd::move(__t)) {}

  // tuple(const tuple<U...>&&) constructors (including allocator_arg_t variants)

  template<class... _Up, enable_if_t<_EnableCtorFromUTypesTuple<const tuple<_Up...> &&>::value> * = nullptr>
  constexpr explicit(!_Lazy<_And, is_convertible<const _Up &&, _Tp>...>::value)
      tuple(const tuple<_Up...> &&__t)
      : __base_(__from_tuple(), kstd::move(__t)) {}

  template<class _Alloc,
           class... _Up,
           enable_if_t<_EnableCtorFromUTypesTuple<const tuple<_Up...> &&>::value> * = nullptr>
  constexpr explicit(!_Lazy<_And, is_convertible<const _Up &&, _Tp>...>::value)
      tuple(allocator_arg_t, const _Alloc &__alloc, const tuple<_Up...> &&__t)
      : __base_(allocator_arg_t(), __alloc, __from_tuple(), kstd::move(__t)) {}

  // tuple(const pair<U1, U2>&) constructors (including allocator_arg_t variants)

  template<template<class...> class _Pred,
           class _Pair,
           class _DecayedPair = __remove_cvref_t<_Pair>,
           class _Tuple = tuple>
  struct _CtorPredicateFromPair : false_type {};

  template<template<class...> class _Pred, class _Pair, class _Up1, class _Up2, class _Tp1, class _Tp2>
  struct _CtorPredicateFromPair<_Pred, _Pair, pair<_Up1, _Up2>, tuple<_Tp1, _Tp2>>
      : _And<_Pred<_Tp1, __copy_cvref_t<_Pair, _Up1>>, _Pred<_Tp2, __copy_cvref_t<_Pair, _Up2>>> {};

  template<class _Pair>
  struct _EnableCtorFromPair : _CtorPredicateFromPair<is_constructible, _Pair> {};

  template<class _Pair>
  struct _NothrowConstructibleFromPair : _CtorPredicateFromPair<is_nothrow_constructible, _Pair> {};

  template<class _Pair, class _DecayedPair = __remove_cvref_t<_Pair>, class _Tuple = tuple>
  struct _BothImplicitlyConvertible : false_type {};

  template<class _Pair, class _Up1, class _Up2, class _Tp1, class _Tp2>
  struct _BothImplicitlyConvertible<_Pair, pair<_Up1, _Up2>, tuple<_Tp1, _Tp2>>
      : _And<is_convertible<__copy_cvref_t<_Pair, _Up1>, _Tp1>, is_convertible<__copy_cvref_t<_Pair, _Up2>, _Tp2>> {};

  template<class _Up1,
           class _Up2,
           template<class...> class _And = _And,
           enable_if_t<_And<_EnableCtorFromPair<const pair<_Up1, _Up2> &>>::value, int> = 0>

  constexpr explicit(_Not<_BothImplicitlyConvertible<const pair<_Up1, _Up2> &>>::value)
      tuple(const pair<_Up1, _Up2> &__p) noexcept(_NothrowConstructibleFromPair<const pair<_Up1, _Up2> &>::value)
      : __base_(__from_tuple(), __p) {}

  template<class _Alloc,
           class _Up1,
           class _Up2,
           template<class...> class _And = _And,
           enable_if_t<_And<_EnableCtorFromPair<const pair<_Up1, _Up2> &>>::value, int> = 0>

  constexpr explicit(_Not<_BothImplicitlyConvertible<const pair<_Up1, _Up2> &>>::value)
      tuple(allocator_arg_t, const _Alloc &__a, const pair<_Up1, _Up2> &__p)
      : __base_(allocator_arg_t(), __a, __from_tuple(), __p) {}

  // tuple(pair<U1, U2>&) constructors (including allocator_arg_t variants)

  template<class _U1, class _U2, enable_if_t<_EnableCtorFromPair<pair<_U1, _U2> &>::value> * = nullptr>
  constexpr explicit(!_BothImplicitlyConvertible<pair<_U1, _U2> &>::value)
      tuple(pair<_U1, _U2> &__p)
      : __base_(__from_tuple(), __p) {}

  template<class _Alloc,
           class _U1,
           class _U2,
           enable_if_t<_EnableCtorFromPair<kstd::pair<_U1, _U2> &>::value> * = nullptr>
  constexpr explicit(!_BothImplicitlyConvertible<pair<_U1, _U2> &>::value)
      tuple(allocator_arg_t, const _Alloc &__alloc, pair<_U1, _U2> &__p)
      : __base_(allocator_arg_t(), __alloc, __from_tuple(), __p) {}

  // tuple(pair<U1, U2>&&) constructors (including allocator_arg_t variants)

  template<class _Up1,
           class _Up2,
           template<class...> class _And = _And,
           enable_if_t<_And<_EnableCtorFromPair<pair<_Up1, _Up2> &&>>::value, int> = 0>

  constexpr explicit(_Not<_BothImplicitlyConvertible<pair<_Up1, _Up2> &&>>::value)
      tuple(pair<_Up1, _Up2> &&__p) noexcept(_NothrowConstructibleFromPair<pair<_Up1, _Up2> &&>::value)
      : __base_(__from_tuple(), kstd::move(__p)) {}

  template<class _Alloc,
           class _Up1,
           class _Up2,
           template<class...> class _And = _And,
           enable_if_t<_And<_EnableCtorFromPair<pair<_Up1, _Up2> &&>>::value, int> = 0>

  constexpr explicit(_Not<_BothImplicitlyConvertible<pair<_Up1, _Up2> &&>>::value)
      tuple(allocator_arg_t, const _Alloc &__a, pair<_Up1, _Up2> &&__p)
      : __base_(allocator_arg_t(), __a, __from_tuple(), kstd::move(__p)) {}

  // tuple(const pair<U1, U2>&&) constructors (including allocator_arg_t variants)

  template<class _U1, class _U2, enable_if_t<_EnableCtorFromPair<const pair<_U1, _U2> &&>::value> * = nullptr>
  constexpr explicit(!_BothImplicitlyConvertible<const pair<_U1, _U2> &&>::value)
      tuple(const pair<_U1, _U2> &&__p)
      : __base_(__from_tuple(), kstd::move(__p)) {}

  template<class _Alloc,
           class _U1,
           class _U2,
           enable_if_t<_EnableCtorFromPair<const pair<_U1, _U2> &&>::value> * = nullptr>
  constexpr explicit(!_BothImplicitlyConvertible<const pair<_U1, _U2> &&>::value)
      tuple(allocator_arg_t, const _Alloc &__alloc, const pair<_U1, _U2> &&__p)
      : __base_(allocator_arg_t(), __alloc, __from_tuple(), kstd::move(__p)) {}

  // [tuple.assign]
  constexpr tuple &
  operator=(_If<_And<is_copy_assignable<_Tp>...>::value, tuple, __nat> const &__tuple) noexcept(
      _And<is_nothrow_copy_assignable<_Tp>...>::value) {
    kstd::__memberwise_copy_assign(*this, __tuple, __index_sequence_for<_Tp...>());
    return *this;
  }

  constexpr const tuple &operator=(tuple const &__tuple) const
    requires(_And<is_copy_assignable<const _Tp>...>::value)
  {
    kstd::__memberwise_copy_assign(*this, __tuple, __index_sequence_for<_Tp...>());
    return *this;
  }

  constexpr const tuple &operator=(tuple &&__tuple) const
    requires(_And<is_assignable<const _Tp &, _Tp>...>::value)
  {
    kstd::__memberwise_forward_assign(*this, kstd::move(__tuple), __type_list<_Tp...>(), __index_sequence_for<_Tp...>());
    return *this;
  }

  constexpr tuple &
  operator=(_If<_And<is_move_assignable<_Tp>...>::value, tuple, __nat> &&__tuple) noexcept(
      _And<is_nothrow_move_assignable<_Tp>...>::value) {
    kstd::__memberwise_forward_assign(*this, kstd::move(__tuple), __type_list<_Tp...>(), __index_sequence_for<_Tp...>());
    return *this;
  }

  template<
      class... _Up,
      enable_if_t<_And<bool_constant<sizeof...(_Tp) == sizeof...(_Up)>, is_assignable<_Tp &, _Up const &>...>::value,
                  int> = 0>
  constexpr tuple &
  operator=(tuple<_Up...> const &__tuple) noexcept(_And<is_nothrow_assignable<_Tp &, _Up const &>...>::value) {
    kstd::__memberwise_copy_assign(*this, __tuple, __index_sequence_for<_Tp...>());
    return *this;
  }

  template<class... _Up,
           enable_if_t<_And<bool_constant<sizeof...(_Tp) == sizeof...(_Up)>, is_assignable<_Tp &, _Up>...>::value,
                       int> = 0>
  constexpr tuple &
  operator=(tuple<_Up...> &&__tuple) noexcept(_And<is_nothrow_assignable<_Tp &, _Up>...>::value) {
    kstd::__memberwise_forward_assign(*this, kstd::move(__tuple), __type_list<_Up...>(), __index_sequence_for<_Tp...>());
    return *this;
  }

  template<class... _UTypes,
           enable_if_t<_And<bool_constant<sizeof...(_Tp) == sizeof...(_UTypes)>,
                            is_assignable<const _Tp &, const _UTypes &>...>::value> * = nullptr>
  constexpr const tuple &operator=(const tuple<_UTypes...> &__u) const {
    kstd::__memberwise_copy_assign(*this, __u, index_sequence_for<_Tp...>());
    return *this;
  }

  template<class... _UTypes,
           enable_if_t<_And<bool_constant<sizeof...(_Tp) == sizeof...(_UTypes)>,
                            is_assignable<const _Tp &, _UTypes>...>::value> * = nullptr>
  constexpr const tuple &operator=(tuple<_UTypes...> &&__u) const {
    kstd::__memberwise_forward_assign(*this, __u, __type_list<_UTypes...>(), index_sequence_for<_Tp...>());
    return *this;
  }

  template<template<class...> class _Pred,
           bool _Const,
           class _Pair,
           class _DecayedPair = __remove_cvref_t<_Pair>,
           class _Tuple = tuple>
  struct _AssignPredicateFromPair : false_type {};

  template<template<class...> class _Pred, bool _Const, class _Pair, class _Up1, class _Up2, class _Tp1, class _Tp2>
  struct _AssignPredicateFromPair<_Pred, _Const, _Pair, pair<_Up1, _Up2>, tuple<_Tp1, _Tp2>>
      : _And<_Pred<__maybe_const<_Const, _Tp1> &, __copy_cvref_t<_Pair, _Up1>>,
             _Pred<__maybe_const<_Const, _Tp2> &, __copy_cvref_t<_Pair, _Up2>>> {};

  template<bool _Const, class _Pair>
  struct _EnableAssignFromPair : _AssignPredicateFromPair<is_assignable, _Const, _Pair> {};

  template<bool _Const, class _Pair>
  struct _NothrowAssignFromPair : _AssignPredicateFromPair<is_nothrow_assignable, _Const, _Pair> {};

  template<class _U1, class _U2, enable_if_t<_EnableAssignFromPair<true, const pair<_U1, _U2> &>::value> * = nullptr>
  constexpr const tuple &operator=(const pair<_U1, _U2> &__pair) const
      noexcept(_NothrowAssignFromPair<true, const pair<_U1, _U2> &>::value) {
    kstd::get<0>(*this) = __pair.first;
    kstd::get<1>(*this) = __pair.second;
    return *this;
  }

  template<class _U1, class _U2, enable_if_t<_EnableAssignFromPair<true, pair<_U1, _U2> &&>::value> * = nullptr>
  constexpr const tuple &operator=(pair<_U1, _U2> &&__pair) const
      noexcept(_NothrowAssignFromPair<true, pair<_U1, _U2> &&>::value) {
    kstd::get<0>(*this) = kstd::move(__pair.first);
    kstd::get<1>(*this) = kstd::move(__pair.second);
    return *this;
  }

  template<class _Up1,
           class _Up2,
           enable_if_t<_EnableAssignFromPair<false, pair<_Up1, _Up2> const &>::value, int> = 0>
  constexpr tuple &
  operator=(pair<_Up1, _Up2> const &__pair) noexcept(_NothrowAssignFromPair<false, pair<_Up1, _Up2> const &>::value) {
    kstd::get<0>(*this) = __pair.first;
    kstd::get<1>(*this) = __pair.second;
    return *this;
  }

  template<class _Up1, class _Up2, enable_if_t<_EnableAssignFromPair<false, pair<_Up1, _Up2> &&>::value, int> = 0>
  constexpr tuple &
  operator=(pair<_Up1, _Up2> &&__pair) noexcept(_NothrowAssignFromPair<false, pair<_Up1, _Up2> &&>::value) {
    kstd::get<0>(*this) = kstd::forward<_Up1>(__pair.first);
    kstd::get<1>(*this) = kstd::forward<_Up2>(__pair.second);
    return *this;
  }

  // EXTENSION
  template<
      class _Up,
      size_t _Np,
      enable_if_t<_And<bool_constant<_Np == sizeof...(_Tp)>, is_assignable<_Tp &, _Up const &>...>::value, int> = 0>
  constexpr tuple &
  operator=(array<_Up, _Np> const &__array) noexcept(_And<is_nothrow_assignable<_Tp &, _Up const &>...>::value) {
    kstd::__memberwise_copy_assign(*this, __array, __index_sequence_for<_Tp...>());
    return *this;
  }

  // EXTENSION
  template<class _Up,
           size_t _Np,
           class = void,
           enable_if_t<_And<bool_constant<_Np == sizeof...(_Tp)>, is_assignable<_Tp &, _Up>...>::value, int> = 0>
  constexpr tuple &
  operator=(array<_Up, _Np> &&__array) noexcept(_And<is_nothrow_assignable<_Tp &, _Up>...>::value) {
    kstd::__memberwise_forward_assign(
        *this, kstd::move(__array), __type_list<_If<true, _Up, _Tp>...>(), __index_sequence_for<_Tp...>());
    return *this;
  }

  // [tuple.swap]
  constexpr void
  swap(tuple &__t) noexcept(__all<is_nothrow_swappable_v<_Tp>...>::value) {
    __base_.swap(__t.__base_);
  }

  constexpr void swap(const tuple &__t) const
      noexcept(__all<is_nothrow_swappable_v<const _Tp &>...>::value) {
    __base_.swap(__t.__base_);
  }

  template<__tuple_like_no_tuple _UTuple>
  friend constexpr bool operator==(const tuple &__x, const _UTuple &__y) {
    static_assert(sizeof...(_Tp) == tuple_size_v<_UTuple>, "Can't compare tuple-like values of different sizes");
    return kstd::__tuple_compare_equal<sizeof...(_Tp)>(__x, __y);
  }

  template<__tuple_like_no_tuple _UTuple>
    requires(sizeof...(_Tp) == tuple_size_v<_UTuple>)
  friend constexpr __tuple_common_comparison_category<tuple, _UTuple>
  operator<=>(const tuple &__x, const _UTuple &__y) {
    return kstd::__tuple_compare_three_way<__tuple_common_comparison_category<tuple, _UTuple>>(
        __x, __y, index_sequence_for<_Tp...>{});
  }
};

template<>
class tuple<> {
  public:
  constexpr tuple() noexcept = default;
  template<class _Alloc>
  constexpr tuple(allocator_arg_t, const _Alloc &) noexcept {}
  template<class _Alloc>
  constexpr tuple(allocator_arg_t, const _Alloc &, const tuple &) noexcept {}
  template<class _Up>
  constexpr tuple(array<_Up, 0>) noexcept {}
  template<class _Alloc, class _Up>
  constexpr tuple(allocator_arg_t, const _Alloc &, array<_Up, 0>) noexcept {}
  constexpr void swap(tuple &) noexcept {}
  constexpr void swap(const tuple &) const noexcept {}

  template<__tuple_like_no_tuple _UTuple>
  friend constexpr bool operator==(const tuple &, const _UTuple &) {
    static_assert(tuple_size_v<_UTuple> == 0, "Can't compare tuple-like values of different sizes");
    return true;
  }

  template<__tuple_like_no_tuple _UTuple>
    requires(tuple_size_v<_UTuple> == 0)
  friend constexpr strong_ordering operator<=>(const tuple &, const _UTuple &) {
    return strong_ordering::equal;
  }
};

template<class... _TTypes, class... _UTypes, template<class> class _TQual, template<class> class _UQual>
  requires requires { typename tuple<common_reference_t<_TQual<_TTypes>, _UQual<_UTypes>>...>; }
struct basic_common_reference<tuple<_TTypes...>, tuple<_UTypes...>, _TQual, _UQual> {
  using type = tuple<common_reference_t<_TQual<_TTypes>, _UQual<_UTypes>>...>;
};

template<class... _TTypes, class... _UTypes>
  requires requires { typename tuple<common_type_t<_TTypes, _UTypes>...>; }
struct common_type<tuple<_TTypes...>, tuple<_UTypes...>> {
  using type = tuple<common_type_t<_TTypes, _UTypes>...>;
};

template<class... _Tp>
tuple(_Tp...) -> tuple<_Tp...>;
template<class _Tp1, class _Tp2>
tuple(pair<_Tp1, _Tp2>) -> tuple<_Tp1, _Tp2>;
template<class _Alloc, class... _Tp>
tuple(allocator_arg_t, _Alloc, _Tp...) -> tuple<_Tp...>;
template<class _Alloc, class _Tp1, class _Tp2>
tuple(allocator_arg_t, _Alloc, pair<_Tp1, _Tp2>) -> tuple<_Tp1, _Tp2>;
template<class _Alloc, class... _Tp>
tuple(allocator_arg_t, _Alloc, tuple<_Tp...>) -> tuple<_Tp...>;

template<class... _Tp, enable_if_t<__all<__is_swappable_v<_Tp>...>::value, int> = 0>
inline constexpr void
swap(tuple<_Tp...> &__t, tuple<_Tp...> &__u) noexcept(__all<is_nothrow_swappable_v<_Tp>...>::value) {
  __t.swap(__u);
}

template<class... _Tp>
constexpr enable_if_t<__all<is_swappable_v<const _Tp>...>::value, void>
swap(const tuple<_Tp...> &__lhs,
     const tuple<_Tp...> &__rhs) noexcept(__all<is_nothrow_swappable_v<const _Tp>...>::value) {
  __lhs.swap(__rhs);
}

// get

template<size_t _Ip, class... _Tp>
[[__nodiscard__]] inline constexpr
    typename tuple_element<_Ip, tuple<_Tp...>>::type &
    get(tuple<_Tp...> &__t) noexcept {
  using type = typename tuple_element<_Ip, tuple<_Tp...>>::type;
  return static_cast<__tuple_leaf<_Ip, type> &>(__t.__base_).get();
}

template<size_t _Ip, class... _Tp>
[[__nodiscard__]] inline constexpr const typename tuple_element<_Ip, tuple<_Tp...>>::type &
get(const tuple<_Tp...> &__t) noexcept {
  using type = typename tuple_element<_Ip, tuple<_Tp...>>::type;
  return static_cast<const __tuple_leaf<_Ip, type> &>(__t.__base_).get();
}

template<size_t _Ip, class... _Tp>
[[__nodiscard__]] inline constexpr
    typename tuple_element<_Ip, tuple<_Tp...>>::type &&
    get(tuple<_Tp...> &&__t) noexcept {
  using type = typename tuple_element<_Ip, tuple<_Tp...>>::type;
  return static_cast<type &&>(static_cast<__tuple_leaf<_Ip, type> &&>(__t.__base_).get());
}

template<size_t _Ip, class... _Tp>
[[__nodiscard__]] inline constexpr const typename tuple_element<_Ip, tuple<_Tp...>>::type &&
get(const tuple<_Tp...> &&__t) noexcept {
  using type = typename tuple_element<_Ip, tuple<_Tp...>>::type;
  return static_cast<const type &&>(static_cast<const __tuple_leaf<_Ip, type> &&>(__t.__base_).get());
}


template<class _T1, class... _Args>
[[__nodiscard__]] inline constexpr _T1 &get(tuple<_Args...> &__tup) noexcept {
  return kstd::get<__find_exactly_one_t<_T1, _Args...>::value>(__tup);
}

template<class _T1, class... _Args>
[[__nodiscard__]] inline constexpr _T1 const &get(tuple<_Args...> const &__tup) noexcept {
  return kstd::get<__find_exactly_one_t<_T1, _Args...>::value>(__tup);
}

template<class _T1, class... _Args>
[[__nodiscard__]] inline constexpr _T1 &&get(tuple<_Args...> &&__tup) noexcept {
  return kstd::get<__find_exactly_one_t<_T1, _Args...>::value>(kstd::move(__tup));
}

template<class _T1, class... _Args>
[[__nodiscard__]] inline constexpr _T1 const &&get(tuple<_Args...> const &&__tup) noexcept {
  return kstd::get<__find_exactly_one_t<_T1, _Args...>::value>(kstd::move(__tup));
}

// tie

template<class... _Tp>
[[__nodiscard__]] inline constexpr tuple<_Tp &...> tie(_Tp &...__t) noexcept {
  return tuple<_Tp &...>(__t...);
}

template<class... _Tp>
[[__nodiscard__]] inline constexpr tuple<__unwrap_ref_decay_t<_Tp>...>
make_tuple(_Tp &&...__t) {
  return tuple<__unwrap_ref_decay_t<_Tp>...>(kstd::forward<_Tp>(__t)...);
}

template<class... _Tp>
[[__nodiscard__]] inline constexpr tuple<_Tp &&...>
forward_as_tuple(_Tp &&...__t) noexcept {
  return tuple<_Tp &&...>(kstd::forward<_Tp>(__t)...);
}

template<class... _Tp, class... _Up>

inline constexpr bool
operator==(const tuple<_Tp...> &__x, const tuple<_Up...> &__y) {
  static_assert(sizeof...(_Tp) == sizeof...(_Up), "Can't compare tuples of different sizes");
  return kstd::__tuple_compare_equal<sizeof...(_Tp)>(__x, __y);
}


// operator<=>

template<class... _Tp, class... _Up>
  requires(sizeof...(_Tp) == sizeof...(_Up))
constexpr common_comparison_category_t<__synth_three_way_result<_Tp, _Up>...>
operator<=>(const tuple<_Tp...> &__x, const tuple<_Up...> &__y) {
  return kstd::__tuple_compare_three_way<common_comparison_category_t<__synth_three_way_result<_Tp, _Up>...>>(
      __x, __y, index_sequence_for<_Tp...>{});
}


// tuple_cat

template<class... _Tuples>
struct __tuple_cat_return_impl;

template<class... _Types>
struct __tuple_cat_return_impl<tuple<_Types...>> {
  using type = tuple<_Types...>;
};

template<class... _Types0, class... _Types1, class... _Tuples>
struct __tuple_cat_return_impl<tuple<_Types0...>, tuple<_Types1...>, _Tuples...>
    : __tuple_cat_return_impl<tuple<_Types0..., _Types1...>, _Tuples...> {};

template<class... _Types0, class _Tp, class _Up, class... _Tuples>
struct __tuple_cat_return_impl<tuple<_Types0...>, pair<_Tp, _Up>, _Tuples...>
    : __tuple_cat_return_impl<tuple<_Types0..., _Tp, _Up>, _Tuples...> {};

template<class, class, class>
struct __tuple_cat_array;

template<class... _Types, class _ValueT, size_t... _Indices>
struct __tuple_cat_array<tuple<_Types...>, _ValueT, __index_sequence<_Indices...>> {
  template<size_t>
  using __value_type = _ValueT;

  using type = tuple<_Types..., __value_type<_Indices>...>;
};

template<class... _Types, class _ValueT, size_t _Np, class... _Tuples>
struct __tuple_cat_return_impl<tuple<_Types...>, array<_ValueT, _Np>, _Tuples...>
    : __tuple_cat_return_impl<typename __tuple_cat_array<tuple<_Types...>, _ValueT, __make_index_sequence<_Np>>::type,
                              _Tuples...> {};

template<class... _Tuples>
using __tuple_cat_return_t =
    typename __tuple_cat_return_impl<tuple<>, __remove_cvref_t<_Tuples>...>::type;

[[__nodiscard__]] inline constexpr tuple<> tuple_cat() { return tuple<>(); }

template<class _Rp, class _Indices, class _Tuple0, class... _Tuples>
struct __tuple_cat_return_ref_imp;

template<class... _Types, size_t... _I0, class _Tuple0>
struct __tuple_cat_return_ref_imp<tuple<_Types...>, __index_sequence<_I0...>, _Tuple0> {
  using _T0 = remove_reference_t<_Tuple0>;
  typedef tuple<_Types..., __copy_cvref_t<_Tuple0, typename tuple_element<_I0, _T0>::type> &&...> type;
};

template<class... _Types, size_t... _I0, class _Tuple0, class _Tuple1, class... _Tuples>
struct __tuple_cat_return_ref_imp<tuple<_Types...>, __index_sequence<_I0...>, _Tuple0, _Tuple1, _Tuples...>
    : public __tuple_cat_return_ref_imp<
          tuple<_Types...,
                __copy_cvref_t<_Tuple0, typename tuple_element<_I0, remove_reference_t<_Tuple0>>::type> &&...>,
          __make_index_sequence<tuple_size<remove_reference_t<_Tuple1>>::value>,
          _Tuple1,
          _Tuples...> {};

template<class _Tuple0, class... _Tuples>
struct __tuple_cat_return_ref
    : public __tuple_cat_return_ref_imp<
          tuple<>,
          __make_index_sequence<tuple_size<remove_reference_t<_Tuple0>>::value>,
          _Tuple0,
          _Tuples...> {};

template<class _Types, class _I0, class _J0>
struct __tuple_cat;

template<class... _Types, size_t... _I0, size_t... _J0>
struct __tuple_cat<tuple<_Types...>, __index_sequence<_I0...>, __index_sequence<_J0...>> {
  template<class _Tuple0>
  constexpr
      typename __tuple_cat_return_ref<tuple<_Types...> &&, _Tuple0 &&>::type
      operator()(tuple<_Types...> __t, _Tuple0 &&__t0) {
    (void) __t;// avoid unused parameter warning on GCC when _I0 is empty
    return kstd::forward_as_tuple(
        kstd::forward<_Types>(kstd::get<_I0>(__t))..., kstd::get<_J0>(kstd::forward<_Tuple0>(__t0))...);
  }

  template<class _Tuple0, class _Tuple1, class... _Tuples>
  constexpr
      typename __tuple_cat_return_ref<tuple<_Types...> &&, _Tuple0 &&, _Tuple1 &&, _Tuples &&...>::type
      operator()(tuple<_Types...> __t, _Tuple0 &&__t0, _Tuple1 &&__t1, _Tuples &&...__tpls) {
    (void) __t;// avoid unused parameter warning on GCC when _I0 is empty
    using _T0 = remove_reference_t<_Tuple0>;
    using _T1 = remove_reference_t<_Tuple1>;
    return __tuple_cat<tuple<_Types..., __copy_cvref_t<_Tuple0, typename tuple_element<_J0, _T0>::type> &&...>,
                       __make_index_sequence<sizeof...(_Types) + tuple_size<_T0>::value>,
                       __make_index_sequence<tuple_size<_T1>::value>>()(
        kstd::forward_as_tuple(
            kstd::forward<_Types>(kstd::get<_I0>(__t))..., kstd::get<_J0>(kstd::forward<_Tuple0>(__t0))...),
        kstd::forward<_Tuple1>(__t1),
        kstd::forward<_Tuples>(__tpls)...);
  }
};

template<class _TupleDst, class _TupleSrc, size_t... _Indices>
inline constexpr _TupleDst
__tuple_cat_select_element_wise(_TupleSrc &&__src, __index_sequence<_Indices...>) {
  static_assert(tuple_size<_TupleDst>::value == tuple_size<_TupleSrc>::value,
                "misuse of __tuple_cat_select_element_wise with tuples of different sizes");
  return _TupleDst(kstd::get<_Indices>(kstd::forward<_TupleSrc>(__src))...);
}

template<class _Tuple0, class... _Tuples>
[[__nodiscard__]] inline constexpr __tuple_cat_return_t<_Tuple0, _Tuples...>
tuple_cat(_Tuple0 &&__t0, _Tuples &&...__tpls) {
  using _T0 = remove_reference_t<_Tuple0>;
  using _TRet = __tuple_cat_return_t<_Tuple0, _Tuples...>;
  using _T0Indices = __make_index_sequence<tuple_size<_T0>::value>;
  using _TRetIndices = __make_index_sequence<tuple_size<_TRet>::value>;
  return kstd::__tuple_cat_select_element_wise<_TRet>(
      __tuple_cat<tuple<>, __index_sequence<>, _T0Indices>()(
          tuple<>(), kstd::forward<_Tuple0>(__t0), kstd::forward<_Tuples>(__tpls)...),
      _TRetIndices());
}

template<class... _Tp, class _Alloc>
struct uses_allocator<tuple<_Tp...>, _Alloc> : true_type {};

#define NOEXCEPTRETURN(...) \
  noexcept(noexcept(__VA_ARGS__)) { return __VA_ARGS__; }

// The NOEXCEPTRETURN macro breaks formatting.
// clang-format off
template <class _Fn, class _Tuple, size_t... _Id>
inline  constexpr decltype(auto)
__apply_tuple_impl(_Fn&& __f, _Tuple&& __t, index_sequence<_Id...>)
    NOEXCEPTRETURN(kstd::invoke(kstd::forward<_Fn>(__f), kstd::get<_Id>(kstd::forward<_Tuple>(__t))...))

template <class _Fn, class _Tuple>
inline  constexpr decltype(auto) apply(_Fn&& __f, _Tuple&& __t)
    NOEXCEPTRETURN(kstd::__apply_tuple_impl(
        kstd::forward<_Fn>(__f),
        kstd::forward<_Tuple>(__t),
        make_index_sequence<tuple_size_v<remove_reference_t<_Tuple>>>()))

template <class _Tp, class _Tuple, size_t... _Idx>
inline  constexpr _Tp __make_from_tuple_impl(_Tuple&& __t, index_sequence<_Idx...>)
  noexcept(noexcept(_Tp(kstd::get<_Idx>(kstd::forward<_Tuple>(__t))...)))
  requires is_constructible_v<_Tp, decltype(kstd::get<_Idx>(kstd::forward<_Tuple>(__t)))...> {
  return _Tp(kstd::get<_Idx>(kstd::forward<_Tuple>(__t))...);
}
#undef NOEXCEPTRETURN

template <class _Tp, class _Tuple,
          class _Seq = make_index_sequence<tuple_size_v<remove_reference_t<_Tuple>>>, class = void>
inline constexpr bool __can_make_from_tuple = false;

template <class _Tp, class _Tuple, size_t... _Idx>
inline constexpr bool __can_make_from_tuple<_Tp, _Tuple, index_sequence<_Idx...>,
    enable_if_t<is_constructible_v<_Tp, decltype(kstd::get<_Idx>(kstd::declval<_Tuple>()))...>>> = true;

// Based on LWG3528(https://wg21.link/LWG3528) and http://eel.is/c++draft/description#structure.requirements-9,
// the standard allows to impose requirements, we constraint kstd::make_from_tuple to make kstd::make_from_tuple
// SFINAE friendly and also avoid worse diagnostic messages. We still keep the constraints of kstd::__make_from_tuple_impl
// so that kstd::__make_from_tuple_impl will have the same advantages when used alone.
template <class _Tp, class _Tuple>
  requires __can_make_from_tuple<_Tp, _Tuple> // strengthen

[[__nodiscard__]] inline  constexpr _Tp make_from_tuple(_Tuple&& __t)
  noexcept(noexcept(kstd::__make_from_tuple_impl<_Tp>(kstd::forward<_Tuple>(__t),
                    make_index_sequence<tuple_size_v<remove_reference_t<_Tuple>>>()))) {
  if constexpr (tuple_size_v<remove_reference_t<_Tuple>> == 1) {
    static_assert(!kstd::reference_constructs_from_temporary_v<_Tp, decltype(kstd::get<0>(kstd::declval<_Tuple>()))>,
                  "Attempted construction of reference element binds to a temporary whose lifetime has ended");
  }
  return kstd::__make_from_tuple_impl<_Tp>(
        kstd::forward<_Tuple>(__t), make_index_sequence<tuple_size_v<remove_reference_t<_Tuple>>>());
}




}
