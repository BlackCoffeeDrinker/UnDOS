
#pragma once
#include <__config.hpp>

#include <__type_traits/bool_constant.hpp>

namespace kstd {
template<typename T>
struct is_enum : bool_constant<__is_enum(T)> {};

template<typename T>
inline constexpr bool is_enum_v = __is_enum(T);
}// namespace kstd
