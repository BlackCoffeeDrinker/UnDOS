
#pragma once

#include <__config.hpp>

namespace kstd {

template<class _Arg, class _Result>
struct __unary_function_keep_layout_base {
};

template<class _Arg, class _Result>
using __unary_function = __unary_function_keep_layout_base<_Arg, _Result>;
}// namespace kstd
