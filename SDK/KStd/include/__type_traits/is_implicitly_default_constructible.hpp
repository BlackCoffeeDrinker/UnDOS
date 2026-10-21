
#pragma once
#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_constructible.hpp>

namespace kstd {
template<class _Tp>
void __test_implicit_default_constructible(_Tp);

template<class _Tp, class = void, class = typename is_default_constructible<_Tp>::type>
struct is_implicitly_default_constructible : false_type {};

template<class _Tp>
struct is_implicitly_default_constructible<_Tp,
                                           decltype(kstd::__test_implicit_default_constructible<_Tp const &>({})),
                                           true_type> : true_type {};

template<class _Tp>
struct is_implicitly_default_constructible<_Tp,
                                           decltype(kstd::__test_implicit_default_constructible<_Tp const &>({})),
                                           false_type> : false_type {};
}// namespace kstd
