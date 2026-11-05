
#pragma once

#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {
template<class _Tp, class _Up>
struct reference_constructs_from_temporary
    : public bool_constant<__reference_constructs_from_temporary(_Tp, _Up)> {};

template<class _Tp, class _Up>
inline constexpr bool reference_constructs_from_temporary_v =
    __reference_constructs_from_temporary(_Tp, _Up);

template<class _Tp, class _Up>
inline const bool __reference_constructs_from_temporary_v = __reference_constructs_from_temporary(_Tp, _Up);

}// namespace kstd
