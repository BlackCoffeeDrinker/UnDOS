
#pragma once
#include <__config.hpp>

#include <__type_traits/is_enum.hpp>

namespace kstd {

template<typename _Tp, bool = is_enum<_Tp>::value>
struct underlying_type {
  using type = __underlying_type(_Tp);
};

template<typename _Tp>
struct underlying_type<_Tp, false> {};

template<typename _Tp>
using underlying_type_t = typename underlying_type<_Tp>::type;

}// namespace kstd
