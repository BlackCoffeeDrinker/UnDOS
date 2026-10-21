
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {

template<template<class...> class _Templ, class... _Args, class = _Templ<_Args...>>
true_type __sfinae_test_impl(int);
template<template<class...> class, class...>
false_type __sfinae_test_impl(...);

template<template<class...> class _Templ, class... _Args>
using _IsValidExpansion = decltype(kstd::__sfinae_test_impl<_Templ, _Args...>(0));

}// namespace kstd
