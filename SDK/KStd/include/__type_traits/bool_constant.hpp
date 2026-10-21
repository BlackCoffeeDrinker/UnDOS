
#pragma once

#include <__config.hpp>
#include <__type_traits/integral_constant.hpp>

namespace kstd {
template<bool v>
using bool_constant = integral_constant<bool, v>;
using true_type = bool_constant<true>;
using false_type = bool_constant<false>;
}// namespace kstd
