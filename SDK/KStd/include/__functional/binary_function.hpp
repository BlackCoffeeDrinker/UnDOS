
#pragma once

#include <__config.hpp>

namespace kstd {

template<class _Arg1, class _Arg2, class _Result>
struct __binary_function_keep_layout_base {
};

template<class _Arg1, class _Arg2, class _Result>
using __binary_function = __binary_function_keep_layout_base<_Arg1, _Arg2, _Result>;

}// namespace kstd
