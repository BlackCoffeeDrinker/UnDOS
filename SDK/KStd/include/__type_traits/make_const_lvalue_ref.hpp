
#pragma once

#include <__config.hpp>
#include <__type_traits/remove_reference.hpp>

namespace kstd {
template<class _Tp>
using __make_const_lvalue_ref = const remove_reference_t<_Tp> &;

}
