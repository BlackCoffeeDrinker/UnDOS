
#pragma once
#include <__config.hpp>

#include <__type_traits/bool_constant.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/remove_cv.hpp>

namespace kstd {

// clang-format off
template <class _Tp> inline const bool __is_floating_point_impl              = false;
template <>          inline const bool __is_floating_point_impl<float>       = true;
template <>          inline const bool __is_floating_point_impl<double>      = true;
template <>          inline const bool __is_floating_point_impl<long double> = true;
// clang-format on

template<class _Tp>
struct is_floating_point
    : integral_constant<bool, __is_floating_point_impl<remove_cv_t<_Tp>>> {};

template<class _Tp>
inline constexpr bool is_floating_point_v = __is_floating_point_impl<remove_cv_t<_Tp>>;
}// namespace kstd
