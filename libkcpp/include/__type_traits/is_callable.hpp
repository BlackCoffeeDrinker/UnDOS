
#pragma once

#include <__config.hpp>

#include <__type_traits/integral_constant.hpp>
#include <__utility/declval.hpp>

namespace kstd {

template <class _Func, class... _Args, class = decltype(kstd::declval<_Func>()(kstd::declval<_Args>()...))>
true_type __is_callable_helper(int);
template <class...>
false_type __is_callable_helper(...);

template <class _Func, class... _Args>
struct __is_callable : decltype(kstd::__is_callable_helper<_Func, _Args...>(0)) {};

}
