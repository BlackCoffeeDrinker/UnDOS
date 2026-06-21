
#pragma once

#include <__config.hpp>

#include <__concepts/arithmetic.hpp>
#include <__concepts/assignable.hpp>
#include <__concepts/common_reference_with.hpp>
#include <__concepts/constructible.hpp>
#include <__concepts/copyable.hpp>
#include <__concepts/derived_from.hpp>
#include <__concepts/equality_comparable.hpp>
#include <__concepts/invocable.hpp>
#include <__concepts/movable.hpp>
#include <__concepts/predicate.hpp>
#include <__concepts/regular.hpp>
#include <__concepts/relation.hpp>
#include <__concepts/same_as.hpp>
#include <__concepts/semiregular.hpp>
#include <__concepts/totally_ordered.hpp>

#include <__iterator/incrementable_traits.hpp>
#include <__iterator/iter_move.hpp>
#include <__iterator/iterator_traits.hpp>

#include <__memory/pointer_traits.hpp>

#include <__type_traits/add_pointer.hpp>
#include <__type_traits/common_reference.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/disjunction.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/invoke.hpp>
#include <__type_traits/is_pointer.hpp>
#include <__type_traits/is_primary_template.hpp>
#include <__type_traits/is_reference.hpp>
#include <__type_traits/is_referenceable.hpp>
#include <__type_traits/is_valid_expansion.hpp>
#include <__type_traits/remove_cv.hpp>
#include <__type_traits/remove_cvref.hpp>
#include <__utility/forward.hpp>

namespace kstd {

// [iterator.concept.readable]
template<class _In>
concept __indirectly_readable_impl =
    requires(const _In __i) {
      typename iter_value_t<_In>;
      typename iter_reference_t<_In>;
      typename iter_rvalue_reference_t<_In>;
      { *__i } -> same_as<iter_reference_t<_In>>;
      { ranges::iter_move(__i) } -> same_as<iter_rvalue_reference_t<_In>>;
    } && common_reference_with<iter_reference_t<_In> &&, iter_value_t<_In> &> &&
    common_reference_with<iter_reference_t<_In> &&, iter_rvalue_reference_t<_In> &&> &&
    common_reference_with<iter_rvalue_reference_t<_In> &&, const iter_value_t<_In> &>;

template<class _In>
concept indirectly_readable = __indirectly_readable_impl<remove_cvref_t<_In>>;

template<class _Tp>
using __projected_iterator_t = typename _Tp::__projected_iterator;

template<class _Tp>
using __projected_projection_t = typename _Tp::__projected_projection;

template<class _Tp>
concept __specialization_of_projected = requires {
  typename __projected_iterator_t<_Tp>;
  typename __projected_projection_t<_Tp>;
} && __is_primary_template<_Tp>::value;

template<class _Tp>
struct __indirect_value_t_impl {
  using type = iter_value_t<_Tp> &;
};
template<__specialization_of_projected _Tp>
struct __indirect_value_t_impl<_Tp> {
  using type =
      invoke_result_t<__projected_projection_t<_Tp> &,
                      typename __indirect_value_t_impl<__projected_iterator_t<_Tp>>::type>;
};

template<indirectly_readable _Tp>
using __indirect_value_t = typename __indirect_value_t_impl<_Tp>::type;

template<indirectly_readable _Tp>
using iter_common_reference_t = common_reference_t<iter_reference_t<_Tp>, __indirect_value_t<_Tp>>;

// [iterator.concept.writable]
template<class _Out, class _Tp>
concept indirectly_writable = requires(_Out &&__o, _Tp &&__t) {
  *__o = kstd::forward<_Tp>(__t);                                             // not required to be equality-preserving
  *kstd::forward<_Out>(__o) = kstd::forward<_Tp>(__t);                        // not required to be equality-preserving
  const_cast<const iter_reference_t<_Out> &&>(*__o) = kstd::forward<_Tp>(__t);// not required to be equality-preserving
  const_cast<const iter_reference_t<_Out> &&>(*kstd::forward<_Out>(__o)) =
      kstd::forward<_Tp>(__t);// not required to be equality-preserving
};

// [iterator.concept.winc]
template<class _Tp>
concept __integer_like = integral<_Tp> && !same_as<_Tp, bool>;

template<class _Tp>
concept __signed_integer_like = signed_integral<_Tp>;

template<class _Ip>
concept weakly_incrementable = movable<_Ip> && requires(_Ip __i) {
  typename iter_difference_t<_Ip>;
  requires __signed_integer_like<iter_difference_t<_Ip>>;
  { ++__i } -> same_as<_Ip &>;// not required to be equality-preserving
  __i++;                      // not required to be equality-preserving
};

// [iterator.concept.inc]
template<class _Ip>
concept incrementable = regular<_Ip> && weakly_incrementable<_Ip> && requires(_Ip __i) {
  { __i++ } -> same_as<_Ip>;
};

// [iterator.concept.iterator]
template<class _Ip>
concept input_or_output_iterator = requires(_Ip __i) {
  { *__i } -> __referenceable;
} && weakly_incrementable<_Ip>;

// [iterator.concept.sentinel]
template<class _Sp, class _Ip>
concept sentinel_for = semiregular<_Sp> && input_or_output_iterator<_Ip> && __weakly_equality_comparable_with<_Sp, _Ip>;

template<class, class>
inline constexpr bool disable_sized_sentinel_for = false;

template<class _Sp, class _Ip>
concept sized_sentinel_for =
    sentinel_for<_Sp, _Ip> && !disable_sized_sentinel_for<remove_cv_t<_Sp>, remove_cv_t<_Ip>> &&
    requires(const _Ip &__i, const _Sp &__s) {
      { __s - __i } -> same_as<iter_difference_t<_Ip>>;
      { __i - __s } -> same_as<iter_difference_t<_Ip>>;
    };

template<class _Iter>
struct __iter_traits_cache {
  using type =
      _If<__is_primary_template<iterator_traits<_Iter>>::value, _Iter, iterator_traits<_Iter>>;
};
template<class _Iter>
using _ITER_TRAITS = typename __iter_traits_cache<_Iter>::type;

struct __iter_concept_concept_test {
  template<class _Iter>
  using _Apply = typename _ITER_TRAITS<_Iter>::iterator_concept;
};
struct __iter_concept_category_test {
  template<class _Iter>
  using _Apply = typename _ITER_TRAITS<_Iter>::iterator_category;
};
struct __iter_concept_random_fallback {
  template<class _Iter>
  using _Apply =
      enable_if_t<__is_primary_template<iterator_traits<_Iter>>::value, random_access_iterator_tag>;
};

template<class _Iter, class _Tester>
struct __test_iter_concept : _IsValidExpansion<_Tester::template _Apply, _Iter>, _Tester {};

template<class _Iter>
struct __iter_concept_cache {
  using type =
      _Or<__test_iter_concept<_Iter, __iter_concept_concept_test>,
          __test_iter_concept<_Iter, __iter_concept_category_test>,
          __test_iter_concept<_Iter, __iter_concept_random_fallback>>;
};

template<class _Iter>
using _ITER_CONCEPT = typename __iter_concept_cache<_Iter>::type::template _Apply<_Iter>;

// [iterator.concept.input]
template<class _Ip>
concept input_iterator = input_or_output_iterator<_Ip> && indirectly_readable<_Ip> && requires {
  typename _ITER_CONCEPT<_Ip>;
} && derived_from<_ITER_CONCEPT<_Ip>, input_iterator_tag>;

// [iterator.concept.output]
template<class _Ip, class _Tp>
concept output_iterator =
    input_or_output_iterator<_Ip> && indirectly_writable<_Ip, _Tp> && requires(_Ip __it, _Tp &&__t) {
      *__it++ = kstd::forward<_Tp>(__t);// not required to be equality-preserving
    };

// [iterator.concept.forward]
template<class _Ip>
concept forward_iterator =
    input_iterator<_Ip> && derived_from<_ITER_CONCEPT<_Ip>, forward_iterator_tag> && incrementable<_Ip> &&
    sentinel_for<_Ip, _Ip>;

// [iterator.concept.bidir]
template<class _Ip>
concept bidirectional_iterator =
    forward_iterator<_Ip> && derived_from<_ITER_CONCEPT<_Ip>, bidirectional_iterator_tag> && requires(_Ip __i) {
      { --__i } -> same_as<_Ip &>;
      { __i-- } -> same_as<_Ip>;
    };

template<class _Ip>
concept random_access_iterator =
    bidirectional_iterator<_Ip> && derived_from<_ITER_CONCEPT<_Ip>, random_access_iterator_tag> &&
    totally_ordered<_Ip> && sized_sentinel_for<_Ip, _Ip> &&
    requires(_Ip __i, const _Ip __j, const iter_difference_t<_Ip> __n) {
      { __i += __n } -> same_as<_Ip &>;
      { __j + __n } -> same_as<_Ip>;
      { __n + __j } -> same_as<_Ip>;
      { __i -= __n } -> same_as<_Ip &>;
      { __j - __n } -> same_as<_Ip>;
      { __j[__n] } -> same_as<iter_reference_t<_Ip>>;
    };

template<class _Ip>
concept contiguous_iterator =
    random_access_iterator<_Ip> && derived_from<_ITER_CONCEPT<_Ip>, contiguous_iterator_tag> &&
    is_lvalue_reference_v<iter_reference_t<_Ip>> && same_as<iter_value_t<_Ip>, remove_cvref_t<iter_reference_t<_Ip>>> &&
    requires(const _Ip &__i) {
      { kstd::to_address(__i) } -> same_as<add_pointer_t<iter_reference_t<_Ip>>>;
    };

template<class _Ip>
concept __has_arrow = input_iterator<_Ip> && (is_pointer_v<_Ip> || requires(_Ip __i) { __i.operator->(); });

// [indirectcallable.indirectinvocable]
template<class _Fp, class _It>
concept indirectly_unary_invocable =
    indirectly_readable<_It> && copy_constructible<_Fp> && invocable<_Fp &, __indirect_value_t<_It>> &&
    invocable<_Fp &, iter_reference_t<_It>> &&
    common_reference_with<invoke_result_t<_Fp &, __indirect_value_t<_It>>,
                          invoke_result_t<_Fp &, iter_reference_t<_It>>>;

template<class _Fp, class _It>
concept indirectly_regular_unary_invocable =
    indirectly_readable<_It> && copy_constructible<_Fp> && regular_invocable<_Fp &, __indirect_value_t<_It>> &&
    regular_invocable<_Fp &, iter_reference_t<_It>> &&
    common_reference_with<invoke_result_t<_Fp &, __indirect_value_t<_It>>,
                          invoke_result_t<_Fp &, iter_reference_t<_It>>>;

template<class _Fp, class _It>
concept indirect_unary_predicate =
    indirectly_readable<_It> && copy_constructible<_Fp> && predicate<_Fp &, __indirect_value_t<_It>> &&
    predicate<_Fp &, iter_reference_t<_It>>;

template<class _Fp, class _It1, class _It2>
concept indirect_binary_predicate =
    indirectly_readable<_It1> && indirectly_readable<_It2> && copy_constructible<_Fp> &&
    predicate<_Fp &, __indirect_value_t<_It1>, __indirect_value_t<_It2>> &&
    predicate<_Fp &, __indirect_value_t<_It1>, iter_reference_t<_It2>> &&
    predicate<_Fp &, iter_reference_t<_It1>, __indirect_value_t<_It2>> &&
    predicate<_Fp &, iter_reference_t<_It1>, iter_reference_t<_It2>>;

template<class _Fp, class _It1, class _It2 = _It1>
concept indirect_equivalence_relation =
    indirectly_readable<_It1> && indirectly_readable<_It2> && copy_constructible<_Fp> &&
    equivalence_relation<_Fp &, __indirect_value_t<_It1>, __indirect_value_t<_It2>> &&
    equivalence_relation<_Fp &, __indirect_value_t<_It1>, iter_reference_t<_It2>> &&
    equivalence_relation<_Fp &, iter_reference_t<_It1>, __indirect_value_t<_It2>> &&
    equivalence_relation<_Fp &, iter_reference_t<_It1>, iter_reference_t<_It2>>;

template<class _Fp, class _It1, class _It2 = _It1>
concept indirect_strict_weak_order =
    indirectly_readable<_It1> && indirectly_readable<_It2> && copy_constructible<_Fp> &&
    strict_weak_order<_Fp &, __indirect_value_t<_It1>, __indirect_value_t<_It2>> &&
    strict_weak_order<_Fp &, __indirect_value_t<_It1>, iter_reference_t<_It2>> &&
    strict_weak_order<_Fp &, iter_reference_t<_It1>, __indirect_value_t<_It2>> &&
    strict_weak_order<_Fp &, iter_reference_t<_It1>, iter_reference_t<_It2>>;

template<class _Fp, class... _Its>
  requires(indirectly_readable<_Its> && ...) && invocable<_Fp, iter_reference_t<_Its>...>
using indirect_result_t = invoke_result_t<_Fp, iter_reference_t<_Its>...>;

template<class _In, class _Out>
concept indirectly_movable = indirectly_readable<_In> && indirectly_writable<_Out, iter_rvalue_reference_t<_In>>;

template<class _In, class _Out>
concept indirectly_movable_storable =
    indirectly_movable<_In, _Out> && indirectly_writable<_Out, iter_value_t<_In>> && movable<iter_value_t<_In>> &&
    constructible_from<iter_value_t<_In>, iter_rvalue_reference_t<_In>> &&
    assignable_from<iter_value_t<_In> &, iter_rvalue_reference_t<_In>>;

template<class _In, class _Out>
concept indirectly_copyable = indirectly_readable<_In> && indirectly_writable<_Out, iter_reference_t<_In>>;

template<class _In, class _Out>
concept indirectly_copyable_storable =
    indirectly_copyable<_In, _Out> && indirectly_writable<_Out, iter_value_t<_In> &> &&
    indirectly_writable<_Out, const iter_value_t<_In> &> && indirectly_writable<_Out, iter_value_t<_In> &&> &&
    indirectly_writable<_Out, const iter_value_t<_In> &&> && copyable<iter_value_t<_In>> &&
    constructible_from<iter_value_t<_In>, iter_reference_t<_In>> &&
    assignable_from<iter_value_t<_In> &, iter_reference_t<_In>>;

// Note: indirectly_swappable is located in iter_swap.h to prevent a dependency cycle
// (both iter_swap and indirectly_swappable require indirectly_readable).


template<class _Tp>
using __has_random_access_iterator_category_or_concept = integral_constant<bool, random_access_iterator<_Tp>>;

}// namespace kstd
