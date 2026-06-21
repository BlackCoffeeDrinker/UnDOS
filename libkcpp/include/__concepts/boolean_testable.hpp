
#pragma once
#include <__config.hpp>
#include <__concepts/convertible_to.hpp>

namespace kstd {

template<class _Tp>
concept __boolean_testable_impl = convertible_to<_Tp, bool>;

template<class _Tp>
concept __boolean_testable = __boolean_testable_impl<_Tp> && requires(_Tp &&__t) {
  { !kstd::forward<_Tp>(__t) } -> __boolean_testable_impl;
};

}// namespace kstd
