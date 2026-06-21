
#pragma once

#include <__config.hpp>
#include <__type_traits/is_base_of.hpp>
#include <__type_traits/is_convertible.hpp>

namespace kstd {

template<class _Dp, class _Bp>
concept derived_from = is_base_of_v<_Bp, _Dp> && is_convertible_v<const volatile _Dp *, const volatile _Bp *>;

}
