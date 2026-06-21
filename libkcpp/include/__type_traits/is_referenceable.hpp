
#pragma once

#include <__config.hpp>
#include <__type_traits/void_t.hpp>

namespace kstd {

template<class _Tp, class = void>
inline const bool __is_referenceable_v = false;

template<class _Tp>
inline const bool __is_referenceable_v<_Tp, __void_t<_Tp &>> = true;

template<class _Tp>
concept __referenceable = __is_referenceable_v<_Tp>;

}// namespace kstd
