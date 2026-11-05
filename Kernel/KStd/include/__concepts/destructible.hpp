
#pragma once
#include <__config.hpp>
#include <__type_traits/is_nothrow_destructible.hpp>

namespace kstd {
template<class _Tp>
concept destructible = is_nothrow_destructible_v<_Tp>;
}
