
#pragma once

#include <__config.hpp>
#include <__type_traits/is_floating_point.hpp>
#include <__type_traits/is_integral.hpp>
#include <__type_traits/is_signed.hpp>

namespace kstd {

// [concepts.arithmetic], arithmetic concepts

template<class _Tp>
concept integral = is_integral_v<_Tp>;

template<class _Tp>
concept signed_integral = integral<_Tp> && is_signed_v<_Tp>;

template<class _Tp>
concept unsigned_integral = integral<_Tp> && !signed_integral<_Tp>;

template<class _Tp>
concept floating_point = is_floating_point_v<_Tp>;

}// namespace kstd
