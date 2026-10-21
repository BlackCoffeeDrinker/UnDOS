
#pragma once

#include <__config.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_empty.hpp>
#include <__type_traits/is_same.hpp>

namespace kstd {

// __make_transparent tries to create a transparent comparator from its non-transparent counterpart, e.g. obtain
// `less<>` from `less<T>`. This is useful in cases where conversions can be avoided (e.g. a string literal to a
// std::string).

template<class _Tp, class _Comparator>
struct __make_transparent {
  using type = _Comparator;
};

template<class _Tp, class _Comparator>
using __make_transparent_t = typename __make_transparent<_Tp, _Comparator>::type;

template<class _Tp,
         class _Comparator,
         enable_if_t<is_same<_Comparator, __make_transparent_t<_Tp, _Comparator>>::value, int> = 0>
_Comparator &__as_transparent(_Comparator &__comp) {
  return __comp;
}

template<class _Tp,
         class _Comparator,
         enable_if_t<!is_same<_Comparator, __make_transparent_t<_Tp, _Comparator>>::value, int> = 0>
__make_transparent_t<_Tp, _Comparator> __as_transparent(_Comparator &) {
  static_assert(is_empty<_Comparator>::value);
  return __make_transparent_t<_Tp, _Comparator>();
}

}// namespace kstd
