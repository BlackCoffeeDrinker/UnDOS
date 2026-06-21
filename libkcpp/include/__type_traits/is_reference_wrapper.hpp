
#pragma once

#include <__config.hpp>
#include <__fwd/functional.hpp>
#include <__type_traits/bool_constant.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/remove_cv.hpp>

namespace kstd {

template<class _Tp>
struct __is_reference_wrapper_impl : false_type {};
template<class _Tp>
struct __is_reference_wrapper_impl<reference_wrapper<_Tp>> : true_type {};
template<class _Tp>
struct __is_reference_wrapper : __is_reference_wrapper_impl<remove_cv_t<_Tp>> {};


}// namespace kstd
