
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {

template<class _Tp>
inline const bool __is_final_v = __is_final(_Tp);

template<class _Tp>
struct is_final : integral_constant<bool, __is_final(_Tp)> {};

template<class _Tp>
inline constexpr bool is_final_v = __is_final(_Tp);


}// namespace kstd
