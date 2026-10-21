
#pragma once

#include <__config.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/is_valid_expansion.hpp>

namespace kstd {

template<class _Tp>
using __test_for_primary_template =
    enable_if_t<is_same<_Tp, typename _Tp::__primary_template>::value>;

template<class _Tp>
using __is_primary_template = _IsValidExpansion<__test_for_primary_template, _Tp>;

}// namespace kstd
