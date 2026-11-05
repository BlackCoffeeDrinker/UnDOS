
#pragma once

#include <__config.hpp>

#include <__algo/comp.hpp>
#include <__algo/comp_ref_type.hpp>
#include <__iterator/iterator_traits.hpp>
#include <__type_traits/is_callable.hpp>


namespace kstd {

template <class _Compare, class _ForwardIterator>
inline _KSTD_API constexpr _ForwardIterator
__max_element(_ForwardIterator __first, _ForwardIterator __last, _Compare __comp) {
  static_assert(
      __has_forward_iterator_category<_ForwardIterator>::value, "std::max_element requires a ForwardIterator");
  if (__first != __last) {
    _ForwardIterator __i = __first;
    while (++__i != __last)
      if (__comp(*__first, *__i))
        __first = __i;
  }
  return __first;
}

template <class _ForwardIterator, class _Compare>
[[__nodiscard__]] inline _KSTD_API constexpr _ForwardIterator
max_element(_ForwardIterator __first, _ForwardIterator __last, _Compare __comp) {
  static_assert(
      __is_callable<_Compare&, decltype(*__first), decltype(*__first)>::value, "The comparator has to be callable");
  return kstd::__max_element<__comp_ref_type<_Compare> >(__first, __last, __comp);
}

template <class _ForwardIterator>
[[__nodiscard__]] inline _KSTD_API constexpr _ForwardIterator
max_element(_ForwardIterator __first, _ForwardIterator __last) {
  return kstd::max_element(__first, __last, __less<>());
}

}
