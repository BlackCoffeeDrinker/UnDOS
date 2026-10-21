
#pragma once

#include <__concepts/equality_comparable.hpp>
#include <__concepts/semiregular.hpp>
#include <__config.hpp>

namespace kstd {

template<class _Tp>
concept regular = semiregular<_Tp> && equality_comparable<_Tp>;

}
