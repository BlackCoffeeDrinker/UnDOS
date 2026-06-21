
#pragma once
#include <__config.hpp>

#include <__type_traits/internal.hpp>
#include <__type_traits/is_floating_point.hpp>
#include <__type_traits/is_integral.hpp>

namespace kstd {
template<typename _Tp>
struct is_arithmetic : detail::__or_<is_integral<_Tp>, is_floating_point<_Tp>>::type {};
}// namespace kstd
