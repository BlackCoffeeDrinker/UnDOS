
#pragma once

#include <__config.hpp>
#include <__type_traits/conditional.hpp>

namespace kstd {

template<bool _Const, class _Tp>
using __maybe_const = __conditional_t<_Const, const _Tp, _Tp>;

}
