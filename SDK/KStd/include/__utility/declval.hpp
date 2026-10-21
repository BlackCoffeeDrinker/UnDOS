
#pragma once
#include <__config.hpp>

namespace kstd {

template<class _Tp>
_Tp &&__declval(int);
template<class _Tp>
_Tp __declval(long);


template<class _Tp>
decltype(kstd::__declval<_Tp>(0)) declval() noexcept {
  static_assert(!__is_same(_Tp, _Tp),
                "std::declval can only be used in an unevaluated context. "
                "It's likely that your current usage is trying to extract a value from the function.");
}
}// namespace kstd
