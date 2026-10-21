
#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>

namespace kstd {

template<class _Pred>
struct _Not : bool_constant<!_Pred::value> {};

template<class _Tp>
struct negation : _Not<_Tp> {};
template<class _Tp>
inline constexpr bool negation_v = !_Tp::value;

}// namespace kstd
