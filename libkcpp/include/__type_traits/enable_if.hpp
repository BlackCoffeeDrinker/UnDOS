
#pragma once
#include <__config.hpp>

namespace kstd {
template<bool, typename _Tp = void>
struct enable_if {};

// Partial specialization for true.
template<typename _Tp>
struct enable_if<true, _Tp> {
  using type = _Tp;
};

template<bool _Cond, typename _Tp = void>
using enable_if_t = typename enable_if<_Cond, _Tp>::type;
}// namespace kstd
