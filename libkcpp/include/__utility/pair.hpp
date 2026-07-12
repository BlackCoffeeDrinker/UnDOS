
#pragma once

#include <__config.hpp>

#include <__compare/common_comparison_category.hpp>
#include <__compare/synth_three_way.hpp>
#include <__concepts/boolean_testable.hpp>
#include <__concepts/different_from.hpp>
#include <__fwd/array.hpp>
#include <__fwd/pair.hpp>
#include <__fwd/tuple.hpp>
#include <__tuple/tuple_like_no_subrange.hpp>
#include <__tuple/tuple_size.hpp>
#include <__type_traits/common_reference.hpp>
#include <__type_traits/common_type.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_assignable.hpp>
#include <__type_traits/is_constructible.hpp>
#include <__type_traits/is_convertible.hpp>
#include <__type_traits/is_implicitly_default_constructible.hpp>
#include <__type_traits/is_nothrow_assignable.hpp>
#include <__type_traits/is_nothrow_constructible.hpp>
#include <__type_traits/is_replaceable.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/is_swappable.hpp>
#include <__type_traits/is_trivially_relocatable.hpp>
#include <__type_traits/nat.hpp>
#include <__type_traits/unwrap_ref.hpp>
#include <__utility/declval.hpp>
#include <__utility/forward.hpp>
#include <__utility/integer_sequence.hpp>
#include <__utility/move.hpp>
#include <__utility/piecewise_construct.hpp>

namespace kstd {


template<class _T1, class _T2>
struct __check_pair_construction {
  template<int &...>
  static _KSTD_API constexpr bool __enable_implicit_default() {
    return is_implicitly_default_constructible<_T1>::value && is_implicitly_default_constructible<_T2>::value;
  }

  template<int &...>
  static _KSTD_API constexpr bool __enable_default() {
    return is_default_constructible<_T1>::value && is_default_constructible<_T2>::value;
  }

  template<class _U1, class _U2>
  static _KSTD_API constexpr bool __is_pair_constructible() {
    return is_constructible<_T1, _U1>::value && is_constructible<_T2, _U2>::value;
  }

  template<class _U1, class _U2>
  static _KSTD_API constexpr bool __is_implicit() {
    return is_convertible<_U1, _T1>::value && is_convertible<_U2, _T2>::value;
  }
};


template<class, class>
struct __non_trivially_copyable_base {
  constexpr _KSTD_API __non_trivially_copyable_base() noexcept {}
  constexpr _KSTD_API
  __non_trivially_copyable_base(__non_trivially_copyable_base const &) noexcept {}
};

template<class _T1, class _T2>
struct pair
#if defined(_LIBCPP_DEPRECATED_ABI_DISABLE_PAIR_TRIVIAL_COPY_CTOR)
    : private __non_trivially_copyable_base<_T1, _T2>
#endif
{
  using first_type = _T1;
  using second_type = _T2;

  _T1 first;
  _T2 second;

  using __trivially_relocatable =
      __conditional_t<__internal_is_trivially_relocatable<_T1>::value && __internal_is_trivially_relocatable<_T2>::value,
                      pair,
                      void>;
  using __replaceable = __conditional_t<is_replaceable_v<_T1> && is_replaceable_v<_T2>, pair, void>;

  _KSTD_API pair(pair const &) = default;
  _KSTD_API pair(pair &&) = default;

  template<class _CheckArgsDep = __check_pair_construction<_T1, _T2>,
           enable_if_t<_CheckArgsDep::__enable_default(), int> = 0>
  explicit(!_CheckArgsDep::__enable_implicit_default()) _KSTD_API constexpr pair() noexcept(
      is_nothrow_default_constructible<first_type>::value && is_nothrow_default_constructible<second_type>::value)
      : first(), second() {}

  template<class _CheckArgsDep = __check_pair_construction<_T1, _T2>,
           enable_if_t<_CheckArgsDep::template __is_pair_constructible<_T1 const &, _T2 const &>(), int> = 0>
  _KSTD_API constexpr explicit(!_CheckArgsDep::template __is_implicit<_T1 const &, _T2 const &>())
      pair(_T1 const &__t1, _T2 const &__t2) noexcept(is_nothrow_copy_constructible<first_type>::value &&
                                                      is_nothrow_copy_constructible<second_type>::value)
      : first(__t1), second(__t2) {}

  template<
      class _U1 = _T1,
      class _U2 = _T2,
      enable_if_t<__check_pair_construction<_T1, _T2>::template __is_pair_constructible<_U1, _U2>(), int> = 0>
  _KSTD_API constexpr explicit(!__check_pair_construction<_T1, _T2>::template __is_implicit<_U1, _U2>())
      pair(_U1 &&__u1, _U2 &&__u2) noexcept(is_nothrow_constructible<first_type, _U1>::value &&
                                            is_nothrow_constructible<second_type, _U2>::value)
      : first(kstd::forward<_U1>(__u1)), second(kstd::forward<_U2>(__u2)) {
  }

  template<class _U1,
           class _U2,
           enable_if_t<__check_pair_construction<_T1, _T2>::template __is_pair_constructible<_U1 &, _U2 &>(), int> = 0>
  _KSTD_API constexpr explicit(!__check_pair_construction<_T1, _T2>::template __is_implicit<_U1 &, _U2 &>())
      pair(pair<_U1, _U2> &__p) noexcept((is_nothrow_constructible<first_type, _U1 &>::value &&
                                          is_nothrow_constructible<second_type, _U2 &>::value))
      : first(__p.first), second(__p.second) {}

  template<
      class _U1,
      class _U2,
      enable_if_t<__check_pair_construction<_T1, _T2>::template __is_pair_constructible<_U1 const &, _U2 const &>(),
                  int> = 0>
  _KSTD_API constexpr explicit(
      !__check_pair_construction<_T1, _T2>::template __is_implicit<_U1 const &, _U2 const &>())
      pair(pair<_U1, _U2> const &__p) noexcept(is_nothrow_constructible<first_type, _U1 const &>::value &&
                                               is_nothrow_constructible<second_type, _U2 const &>::value)
      : first(__p.first), second(__p.second) {}

  template<class _U1,
           class _U2,
           enable_if_t<__check_pair_construction<_T1, _T2>::template __is_pair_constructible<_U1, _U2>(), int> = 0>
  _KSTD_API constexpr explicit(!__check_pair_construction<_T1, _T2>::template __is_implicit<_U1, _U2>())
      pair(pair<_U1, _U2> &&__p) noexcept(is_nothrow_constructible<first_type, _U1 &&>::value &&
                                          is_nothrow_constructible<second_type, _U2 &&>::value)
      : first(kstd::forward<_U1>(__p.first)), second(kstd::forward<_U2>(__p.second)) {}

  template<
      class _U1,
      class _U2,
      enable_if_t<__check_pair_construction<_T1, _T2>::template __is_pair_constructible<const _U1 &&, const _U2 &&>(),
                  int> = 0>
  _KSTD_API constexpr explicit(
      !__check_pair_construction<_T1, _T2>::template __is_implicit<const _U1 &&, const _U2 &&>())
      pair(const pair<_U1, _U2> &&__p) noexcept(is_nothrow_constructible<first_type, const _U1 &&>::value &&
                                                is_nothrow_constructible<second_type, const _U2 &&>::value)
      : first(kstd::move(__p.first)), second(kstd::move(__p.second)) {}

  template<__pair_like_no_subrange _PairLike>
    requires(is_constructible_v<first_type, decltype(kstd::get<0>(kstd::declval<_PairLike &&>()))> &&
             is_constructible_v<second_type, decltype(kstd::get<1>(kstd::declval<_PairLike &&>()))>)
  _KSTD_API constexpr explicit(
      !is_convertible_v<decltype(kstd::get<0>(kstd::declval<_PairLike &&>())), first_type> ||
      !is_convertible_v<decltype(kstd::get<1>(kstd::declval<_PairLike &&>())), second_type>) pair(_PairLike &&__p)
      : first(kstd::get<0>(kstd::forward<_PairLike>(__p))), second(kstd::get<1>(kstd::forward<_PairLike>(__p))) {}

  template<class... _Args1, class... _Args2>
  _KSTD_API constexpr pair(piecewise_construct_t __pc, tuple<_Args1...> __first_args, tuple<_Args2...> __second_args) noexcept(
      is_nothrow_constructible<first_type, _Args1...>::value && is_nothrow_constructible<second_type, _Args2...>::value)
      : pair(__pc, __first_args, __second_args, __index_sequence_for<_Args1...>(), __index_sequence_for<_Args2...>()) {}

  _KSTD_API constexpr pair &
  operator=(__conditional_t<is_copy_assignable<first_type>::value && is_copy_assignable<second_type>::value,
                            pair,
                            __nat> const &__p) noexcept(is_nothrow_copy_assignable<first_type>::value &&
                                                        is_nothrow_copy_assignable<second_type>::value) {
    first = __p.first;
    second = __p.second;
    return *this;
  }

  _KSTD_API constexpr pair &operator=(
      __conditional_t<is_move_assignable<first_type>::value && is_move_assignable<second_type>::value, pair, __nat> &&
          __p) noexcept(is_nothrow_move_assignable<first_type>::value &&
                        is_nothrow_move_assignable<second_type>::value) {
    first = kstd::forward<first_type>(__p.first);
    second = kstd::forward<second_type>(__p.second);
    return *this;
  }

  template<
      class _U1,
      class _U2,
      enable_if_t<is_assignable<first_type &, _U1 const &>::value && is_assignable<second_type &, _U2 const &>::value,
                  int> = 0>
  _KSTD_API constexpr pair &operator=(pair<_U1, _U2> const &__p) {
    first = __p.first;
    second = __p.second;
    return *this;
  }

  template<class _U1,
           class _U2,
           enable_if_t<is_assignable<first_type &, _U1>::value && is_assignable<second_type &, _U2>::value, int> = 0>
  _KSTD_API constexpr pair &operator=(pair<_U1, _U2> &&__p) {
    first = kstd::forward<_U1>(__p.first);
    second = kstd::forward<_U2>(__p.second);
    return *this;
  }

  template<class = void>
  _KSTD_API constexpr const pair &operator=(pair const &__p) const
      noexcept(is_nothrow_copy_assignable_v<const first_type> && is_nothrow_copy_assignable_v<const second_type>)
    requires(is_copy_assignable_v<const first_type> && is_copy_assignable_v<const second_type>)
  {
    first = __p.first;
    second = __p.second;
    return *this;
  }

  template<class = void>
  _KSTD_API constexpr const pair &operator=(pair &&__p) const
      noexcept(is_nothrow_assignable_v<const first_type &, first_type> &&
               is_nothrow_assignable_v<const second_type &, second_type>)
    requires(is_assignable_v<const first_type &, first_type> && is_assignable_v<const second_type &, second_type>)
  {
    first = kstd::forward<first_type>(__p.first);
    second = kstd::forward<second_type>(__p.second);
    return *this;
  }

  template<class _U1, class _U2>
  _KSTD_API constexpr const pair &operator=(const pair<_U1, _U2> &__p) const
    requires(is_assignable_v<const first_type &, const _U1 &> && is_assignable_v<const second_type &, const _U2 &>)
  {
    first = __p.first;
    second = __p.second;
    return *this;
  }

  template<class _U1, class _U2>
  _KSTD_API constexpr const pair &operator=(pair<_U1, _U2> &&__p) const
    requires(is_assignable_v<const first_type &, _U1> && is_assignable_v<const second_type &, _U2>)
  {
    first = kstd::forward<_U1>(__p.first);
    second = kstd::forward<_U2>(__p.second);
    return *this;
  }

  template<__pair_like_no_subrange _PairLike>
    requires(__different_from<_PairLike, pair> &&
             is_assignable_v<first_type &, decltype(kstd::get<0>(kstd::declval<_PairLike>()))> &&
             is_assignable_v<second_type &, decltype(kstd::get<1>(kstd::declval<_PairLike>()))>)
  _KSTD_API constexpr pair &operator=(_PairLike &&__p) {
    first = kstd::get<0>(kstd::forward<_PairLike>(__p));
    second = kstd::get<1>(kstd::forward<_PairLike>(__p));
    return *this;
  }

  template<__pair_like_no_subrange _PairLike>
    requires(__different_from<_PairLike, pair> &&
             is_assignable_v<first_type const &, decltype(kstd::get<0>(kstd::declval<_PairLike>()))> &&
             is_assignable_v<second_type const &, decltype(kstd::get<1>(kstd::declval<_PairLike>()))>)
  _KSTD_API constexpr pair const &operator=(_PairLike &&__p) const {
    first = kstd::get<0>(kstd::forward<_PairLike>(__p));
    second = kstd::get<1>(kstd::forward<_PairLike>(__p));
    return *this;
  }

  // Prior to C++23, we provide an approximation of constructors and assignment operators from
  // pair-like types. This was historically provided as an extension.


  _KSTD_API constexpr void swap(pair &__p)
      noexcept(is_nothrow_swappable_v<first_type> &&is_nothrow_swappable_v<second_type>) {
    using kstd::swap;
    swap(first, __p.first);
    swap(second, __p.second);
  }

  _KSTD_API constexpr void swap(const pair &__p) const
      noexcept(__is_nothrow_swappable_v<const first_type> && __is_nothrow_swappable_v<const second_type>) {
    using kstd::swap;
    swap(first, __p.first);
    swap(second, __p.second);
  }

  private:
  template<class... _Args1, class... _Args2, size_t... _I1, size_t... _I2>
  _KSTD_API constexpr pair(piecewise_construct_t,
                           tuple<_Args1...> &__first_args,
                           tuple<_Args2...> &__second_args,
                           __index_sequence<_I1...>,
                           __index_sequence<_I2...>)
      : first(kstd::forward<_Args1>(kstd::get<_I1>(__first_args))...),
        second(kstd::forward<_Args2>(kstd::get<_I2>(__second_args))...) {}
};

template<class _T1, class _T2>
pair(_T1, _T2) -> pair<_T1, _T2>;

// [pairs.spec], specialized algorithms

template<class _T1, class _T2, class _U1, class _U2>
inline _KSTD_API constexpr bool
operator==(const pair<_T1, _T2> &__x, const pair<_U1, _U2> &__y)
  requires requires {
    { __x.first == __y.first } -> __boolean_testable;
    { __x.second == __y.second } -> __boolean_testable;
  }
{
  return __x.first == __y.first && __x.second == __y.second;
}


template<class _T1, class _T2, class _U1, class _U2>
_KSTD_API constexpr common_comparison_category_t<__synth_three_way_result<_T1, _U1>,
                                                 __synth_three_way_result<_T2, _U2>>
operator<=>(const pair<_T1, _T2> &__x, const pair<_U1, _U2> &__y) {
  if (auto __c = kstd::__synth_three_way(__x.first, __y.first); __c != 0) {
    return __c;
  }
  return kstd::__synth_three_way(__x.second, __y.second);
}

template<class _T1, class _T2, class _U1, class _U2, template<class> class _TQual, template<class> class _UQual>
  requires requires {
    typename pair<common_reference_t<_TQual<_T1>, _UQual<_U1>>, common_reference_t<_TQual<_T2>, _UQual<_U2>>>;
  }
struct basic_common_reference<pair<_T1, _T2>, pair<_U1, _U2>, _TQual, _UQual> {
  using type =
      pair<common_reference_t<_TQual<_T1>, _UQual<_U1>>, common_reference_t<_TQual<_T2>, _UQual<_U2>>>;
};

template<class _T1, class _T2, class _U1, class _U2>
  requires requires { typename pair<common_type_t<_T1, _U1>, common_type_t<_T2, _U2>>; }
struct common_type<pair<_T1, _T2>, pair<_U1, _U2>> {
  using type = pair<common_type_t<_T1, _U1>, common_type_t<_T2, _U2>>;
};

template<class _T1, class _T2, enable_if_t<__is_swappable_v<_T1> && __is_swappable_v<_T2>, int> = 0>
inline _KSTD_API constexpr void swap(pair<_T1, _T2> &__x, pair<_T1, _T2> &__y) noexcept(__is_nothrow_swappable_v<_T1> && __is_nothrow_swappable_v<_T2>) {
  __x.swap(__y);
}

template<class _T1, class _T2>
  requires(__is_swappable_v<const _T1> && __is_swappable_v<const _T2>)
_KSTD_API constexpr void
swap(const pair<_T1, _T2> &__x, const pair<_T1, _T2> &__y) noexcept(noexcept(__x.swap(__y))) {
  __x.swap(__y);
}

template<class _T1, class _T2>
[[__nodiscard__]] inline _KSTD_API constexpr pair<__unwrap_ref_decay_t<_T1>, __unwrap_ref_decay_t<_T2>> make_pair(_T1 &&__t1, _T2 &&__t2) {
  return pair<__unwrap_ref_decay_t<_T1>, __unwrap_ref_decay_t<_T2>>(kstd::forward<_T1>(__t1), kstd::forward<_T2>(__t2));
}

template<class _T1, class _T2>
struct tuple_size<pair<_T1, _T2>> : public integral_constant<size_t, 2> {};

template<size_t _Ip, class _T1, class _T2>
struct tuple_element<_Ip, pair<_T1, _T2>> {
  static_assert(_Ip < 2, "Index out of bounds in kstd::tuple_element<kstd::pair<T1, T2>>");
};

template<class _T1, class _T2>
struct tuple_element<0, pair<_T1, _T2>> {
  using type = _T1;
};

template<class _T1, class _T2>
struct tuple_element<1, pair<_T1, _T2>> {
  using type = _T2;
};

template<size_t _Ip>
struct __get_pair;

template<>
struct __get_pair<0> {
  template<class _T1, class _T2>
  static _KSTD_API constexpr _T1 &get(pair<_T1, _T2> &__p) noexcept {
    return __p.first;
  }

  template<class _T1, class _T2>
  static _KSTD_API constexpr const _T1 &get(const pair<_T1, _T2> &__p) noexcept {
    return __p.first;
  }

  template<class _T1, class _T2>
  static _KSTD_API constexpr _T1 &&get(pair<_T1, _T2> &&__p) noexcept {
    return kstd::forward<_T1>(__p.first);
  }

  template<class _T1, class _T2>
  static _KSTD_API constexpr const _T1 &&get(const pair<_T1, _T2> &&__p) noexcept {
    return kstd::forward<const _T1>(__p.first);
  }
};

template<>
struct __get_pair<1> {
  template<class _T1, class _T2>
  static _KSTD_API constexpr _T2 &get(pair<_T1, _T2> &__p) noexcept {
    return __p.second;
  }

  template<class _T1, class _T2>
  static _KSTD_API constexpr const _T2 &get(const pair<_T1, _T2> &__p) noexcept {
    return __p.second;
  }

  template<class _T1, class _T2>
  static _KSTD_API constexpr _T2 &&get(pair<_T1, _T2> &&__p) noexcept {
    return kstd::forward<_T2>(__p.second);
  }

  template<class _T1, class _T2>
  static _KSTD_API constexpr const _T2 &&get(const pair<_T1, _T2> &&__p) noexcept {
    return kstd::forward<const _T2>(__p.second);
  }
};

template<size_t _Ip, class _T1, class _T2>
[[__nodiscard__]] inline _KSTD_API constexpr
    typename tuple_element<_Ip, pair<_T1, _T2>>::type &
    get(pair<_T1, _T2> &__p) noexcept {
  return __get_pair<_Ip>::get(__p);
}

template<size_t _Ip, class _T1, class _T2>
[[__nodiscard__]] inline _KSTD_API constexpr const typename tuple_element<_Ip, pair<_T1, _T2>>::type &
get(const pair<_T1, _T2> &__p) noexcept {
  return __get_pair<_Ip>::get(__p);
}

template<size_t _Ip, class _T1, class _T2>
[[__nodiscard__]] inline _KSTD_API constexpr
    typename tuple_element<_Ip, pair<_T1, _T2>>::type &&
    get(pair<_T1, _T2> &&__p) noexcept {
  return __get_pair<_Ip>::get(kstd::move(__p));
}

template<size_t _Ip, class _T1, class _T2>
[[__nodiscard__]] inline _KSTD_API constexpr const typename tuple_element<_Ip, pair<_T1, _T2>>::type &&
get(const pair<_T1, _T2> &&__p) noexcept {
  return __get_pair<_Ip>::get(kstd::move(__p));
}

template<class _T1, class _T2>
[[__nodiscard__]] inline _KSTD_API constexpr _T1 &get(pair<_T1, _T2> &__p) noexcept {
  return __p.first;
}

template<class _T1, class _T2>
[[__nodiscard__]] inline _KSTD_API constexpr _T1 const &get(pair<_T1, _T2> const &__p) noexcept {
  return __p.first;
}

template<class _T1, class _T2>
[[__nodiscard__]] inline _KSTD_API constexpr _T1 &&get(pair<_T1, _T2> &&__p) noexcept {
  return kstd::forward<_T1 &&>(__p.first);
}

template<class _T1, class _T2>
[[__nodiscard__]] inline _KSTD_API constexpr _T1 const &&get(pair<_T1, _T2> const &&__p) noexcept {
  return kstd::forward<_T1 const &&>(__p.first);
}

template<class _T2, class _T1>
[[__nodiscard__]] inline _KSTD_API constexpr _T2 &get(pair<_T1, _T2> &__p) noexcept {
  return __p.second;
}

template<class _T2, class _T1>
[[__nodiscard__]] inline _KSTD_API constexpr _T2 const &get(pair<_T1, _T2> const &__p) noexcept {
  return __p.second;
}

template<class _T2, class _T1>
[[__nodiscard__]] inline _KSTD_API constexpr _T2 &&get(pair<_T1, _T2> &&__p) noexcept {
  return kstd::forward<_T2 &&>(__p.second);
}

template<class _T2, class _T1>
[[__nodiscard__]] inline _KSTD_API constexpr _T2 const &&get(pair<_T1, _T2> const &&__p) noexcept {
  return kstd::forward<_T2 const &&>(__p.second);
}


}// namespace kstd
