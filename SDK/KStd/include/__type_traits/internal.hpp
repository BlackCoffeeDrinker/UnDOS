
#pragma once
#include <__config.hpp>
#include <__type_traits/bool_constant.hpp>
#include <__type_traits/enable_if.hpp>

namespace kstd::detail {
// A variadic alias template that resolves to its first argument.
template<typename _Tp, typename...>
using __first_t = _Tp;

template<typename... _Bn>
auto __or_fn(int) -> __first_t<false_type, enable_if_t<!static_cast<bool>(_Bn::value)>...>;

template<typename... _Bn>
auto __or_fn(...) -> true_type;

template<typename... _Bn>
auto __and_fn(int) -> __first_t<true_type, enable_if_t<static_cast<bool>(_Bn::value)>...>;

template<typename... _Bn>
auto __and_fn(...) -> false_type;

template<typename... _Bn>
struct __or_ : decltype(__or_fn<_Bn...>(0)) {};

template<typename... _Bn>
struct __and_ : decltype(__and_fn<_Bn...>(0)) {};

template<typename _Pp>
struct __not_ : bool_constant<!bool(_Pp::value)> {};
}// namespace kstd::detail
