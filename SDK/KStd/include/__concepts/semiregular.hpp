
#pragma once

#include <__concepts/constructible.hpp>
#include <__concepts/copyable.hpp>
#include <__config.hpp>

namespace kstd {
template<class _Tp>
concept semiregular = copyable<_Tp> && default_initializable<_Tp>;

}
