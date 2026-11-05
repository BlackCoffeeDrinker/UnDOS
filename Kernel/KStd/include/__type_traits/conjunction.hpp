
#pragma once

#include <__config.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_same.hpp>

namespace kstd {

template<class...>
using __expand_to_true = true_type;

template<class... _Pred>
__expand_to_true<enable_if_t<_Pred::value>...> __and_helper(int);

template<class...>
false_type __and_helper(...);

// _And always performs lazy evaluation of its arguments.
//
// However, `_And<_Pred...>` itself will evaluate its result immediately (without having to
// be instantiated) since it is an alias, unlike `conjunction<_Pred...>`, which is a struct.
// If you want to defer the evaluation of `_And<_Pred...>` itself, use `_Lazy<_And, _Pred...>`.
template<class... _Pred>
using _And = decltype(kstd::__and_helper<_Pred...>(0));

template<bool... _Preds>
struct __all_dummy;

template<bool... _Pred>
struct __all : _IsSame<__all_dummy<_Pred...>, __all_dummy<((void) _Pred, true)...>> {};


template<class...>
struct conjunction : true_type {};

template<class _Arg>
struct conjunction<_Arg> : _Arg {};

template<class _Arg, class... _Args>
struct conjunction<_Arg, _Args...> : conditional_t<!bool(_Arg::value), _Arg, conjunction<_Args...>> {};

template<class... _Args>
inline constexpr bool conjunction_v = conjunction<_Args...>::value;


}// namespace kstd
