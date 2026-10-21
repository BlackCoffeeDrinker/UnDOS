
#pragma once

#include <__config.hpp>

namespace kstd {

template<class _Tp, class _Up>
concept __different_from = !same_as<remove_cvref_t<_Tp>, remove_cvref_t<_Up>>;

}
