
#pragma once

#include <__config.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/is_trivially_copyable.hpp>

namespace kstd {
#if __has_builtin(__is_trivially_relocatable) && 0
template<class _Tp, class = void>
struct __internal_is_trivially_relocatable : integral_constant<bool, __is_trivially_relocatable(_Tp)> {};
#else
template<class _Tp, class = void>
struct __internal_is_trivially_relocatable : is_trivially_copyable<_Tp> {};
#endif

template<class _Tp>
struct __internal_is_trivially_relocatable<_Tp,
                                           enable_if_t<is_same<_Tp, typename _Tp::__trivially_relocatable>::value>>
    : true_type {};

}// namespace kstd
