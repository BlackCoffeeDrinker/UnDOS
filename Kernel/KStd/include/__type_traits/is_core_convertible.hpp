
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {

template<class _Tp, class _Up, class = void>
inline const bool __is_core_convertible_v = false;

template<class _Tp, class _Up>
inline const bool
    __is_core_convertible_v<_Tp, _Up, decltype(static_cast<void (*)(_Up)>(0)(static_cast<_Tp (*)()>(0)()))> = true;

template<class _Tp, class _Up>
using __is_core_convertible = integral_constant<bool, __is_core_convertible_v<_Tp, _Up>>;


template<class _Tp, class _Up>
concept __core_convertible_to = __is_core_convertible_v<_Tp, _Up>;


template<class _Tp, class _Up, bool = __is_core_convertible_v<_Tp, _Up>>
inline const bool __is_nothrow_core_convertible_v = false;

template<class _Tp, class _Up>
inline const bool __is_nothrow_core_convertible_v<_Tp, _Up, true> =
    noexcept(static_cast<void (*)(_Up) noexcept>(0)(static_cast<_Tp (*)() noexcept>(0)()));

}// namespace kstd
